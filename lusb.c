#include <assert.h>
#include <limits.h>
#include <string.h>
#include <math.h>

#include <libusb.h>

#include "lua.h"
#include "lauxlib.h"

#if LUA_VERSION_NUM<502
typedef unsigned int	lua_Unsigned;
#define lua_rawlen(L,i)		lua_objlen(L,(i))
#define luaL_len(L,i)		lua_objlen(L,(i))
#define lua_tounsigned(L,i)	(lua_Unsigned)lua_tointeger(L,(i))
#define luaL_checkunsigned(L,n)	(lua_Unsigned)luaL_checkinteger(L,(n))
#define luaL_optunsigned(L,n,d)	(lua_Unsigned)luaL_optinteger(L,(n),(d))
#define luaL_setfuncs(L,l,n)	luaI_openlib(L,NULL,(l),(n))
#define luaL_newlibtable(L,l)	lua_createtable(L,0,sizeof(l)/sizeof((l)[0])-1)
#define luaL_newlib(L,l)	(luaL_newlibtable(L,l),luaL_setfuncs(L,l,0))
#endif

static lua_Unsigned l_checkunsigned(lua_State *L, int narg, int nval)
{
#if LUA_VERSION_NUM>=502
    int isnum;
    lua_Unsigned d = lua_tounsignedx(L, nval, &isnum);
    if (!isnum)
    {
	const char *msg = lua_pushfstring(L, "%s expected, got %s",
					  lua_typename(L, LUA_TNUMBER),
					  luaL_typename(L, nval));
	luaL_argerror(L, narg, msg);
    }
    return d;
#else
    lua_Unsigned d;
    if (!lua_isnumber(L, nval))
    {
	const char *msg = lua_pushfstring(L, "%s expected, got %s",
					  lua_typename(L, LUA_TNUMBER),
					  luaL_typename(L, nval));
	luaL_argerror(L, narg, msg);
    }
    return lua_tointeger(L, nval);
#endif
}

#if LUA_VERSION_NUM < 502
char* luaL_prepbuffsize(luaL_Buffer *B, size_t sz)
{
    return (char*)lua_newuserdata(B->L, sz);
}

void luaL_addbuffsize(luaL_Buffer *B, size_t sz)
{
    if (sz > 0)
    {
	char *str = (char*)lua_touserdata(B->L, -1);
	lua_pushlstring(B->L, str, sz);
	lua_remove(B->L, -2);
	luaL_addvalue(B);
    }
    else
	lua_pop(B->L, 1);
}
#else
#define luaL_addbuffsize(B,s)	luaL_addsize(B,(s))
#endif


/* not included in libusb */
struct usb_descriptor
{
    uint8_t bLength;
    uint8_t bDescriptorType;
};

struct usb_string_descriptor
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bString[127];
};

struct usb_hub_descriptor
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bNumberOfPorts;
    uint16_t wHubCharacteristics;
    uint8_t bPowerOnToPowerGood;
    uint8_t bHubControlCurrent;
    uint8_t bRemoveAndPowerMask[64];
};

struct usb_hid_descriptor_list
{
    uint8_t bReportType;
    uint16_t wReportLength;
} ;
struct usb_hid_descriptor
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdHID;
    uint8_t bCountryCode;
    uint8_t bNumDescriptors;
    struct usb_hid_descriptor_list DescriptorList[1];
};


#define CONTEXT_MT	"libusb1_context"
#define DEVICE_MT	"libusb1_device"
#define HANDLE_MT	"libusb1_device_handle"
#define TRANSFER_MT	"libusb1_transfer"
#define DEFAULT_CTX	"libusb1 default context"
#define DEVICES_REG	"libusb1 devices"
#define DEVPTR_REG	"libusb1 devptr"
#define HANDLES_REG	"libusb1 handles"
#define TRANSFER_REG	"libusb1 active transfers"
#define BUFFER_REG	"libusb1 transfer buffers"
#define POLLIN_REG	"libusb1 pollfds in"
#define POLLOUT_REG	"libusb1 pollfds out"

/* NULL is a valid context */
#define INVALID_CONTEXT	((libusb_context*)1)
#define INVALID_DEVICE	((libusb_device*)0)
#define INVALID_HANDLE	((libusb_device_handle*)0)
#define INVALID_TRANSFER ((struct libusb_transfer*)0)


static int _err(lua_State *L, int err)
{
    if (err == LIBUSB_SUCCESS)
    {
	lua_pushboolean(L, 1);
	return 1;
    }
    lua_pushnil(L);
    switch (err)
    {
	case LIBUSB_ERROR_IO:
	    lua_pushliteral(L, "input/output error"); break;
	case LIBUSB_ERROR_INVALID_PARAM:
	    lua_pushliteral(L, "invalid parameter"); break;
	case LIBUSB_ERROR_ACCESS:
	    lua_pushliteral(L, "access denied (insufficient permissions)"); break;
	case LIBUSB_ERROR_NO_DEVICE:
	    lua_pushliteral(L, "no such device (it may have been disconnected)"); break;
	case LIBUSB_ERROR_NOT_FOUND:
	    lua_pushliteral(L, "entity not found"); break;
	case LIBUSB_ERROR_BUSY:
	    lua_pushliteral(L, "resource busy"); break;
	case LIBUSB_ERROR_TIMEOUT:
	    lua_pushliteral(L, "operation timed out"); break;
	case LIBUSB_ERROR_OVERFLOW:
	    lua_pushliteral(L, "overflow"); break;
	case LIBUSB_ERROR_PIPE:
	    lua_pushliteral(L, "pipe error"); break;
	case LIBUSB_ERROR_INTERRUPTED:
	    lua_pushliteral(L, "system call interrupted (perhaps due to signal)"); break;
	case LIBUSB_ERROR_NO_MEM:
	    lua_pushliteral(L, "insufficient memory"); break;
	case LIBUSB_ERROR_NOT_SUPPORTED:
	    lua_pushliteral(L, "operation not supported or unimplemented on this platform"); break;
	case LIBUSB_ERROR_OTHER:
	    lua_pushliteral(L, "other error"); break;
	default:
	    lua_pushfstring(L, "unknown error (0x%x)", err); break;
    }
    lua_pushinteger(L, err);
    return 3;
}

static libusb_context** newctx(lua_State *L)
{
    libusb_context **ctx;
    ctx = (libusb_context**)lua_newuserdata(L, sizeof(libusb_context*));
    *ctx = INVALID_CONTEXT;
    luaL_getmetatable(L, CONTEXT_MT);
    lua_setmetatable(L, -2);
    return ctx;
}

static libusb_context* defctx(lua_State *L)
{
    libusb_context **ctx;
    int err;
    lua_getfield(L, LUA_REGISTRYINDEX, DEFAULT_CTX);
    if (lua_isnil(L, -1))
    {
	lua_pop(L, 1);
	ctx = newctx(L);
	*ctx = NULL;
	if ((err = libusb_init(NULL)) != 0)
	{
	    luaL_where(L, 1);
	    _err(L, err);
	    lua_pop(L, 1);
	    lua_concat(L, 2);
	    lua_error(L);
	    return NULL;
	}
	lua_pushvalue(L, -1);
	lua_setfield(L, LUA_REGISTRYINDEX, DEFAULT_CTX);
    }
    else
	ctx = (libusb_context**)lua_touserdata(L, -1);
    return *ctx;
}

static void devptr(lua_State *L, int dev, libusb_device *ptr)
{
    lua_pushvalue(L, dev);
    lua_pushlightuserdata(L, ptr);
    lua_getfield(L, LUA_REGISTRYINDEX, DEVPTR_REG);
    lua_insert(L, -3);
    lua_settable(L, -3);
    lua_pop(L, 1);
}

static libusb_device** newdev(lua_State *L, int object, libusb_device *dev)
{
    libusb_device **udev;
    if (object < 0)
	object = lua_gettop(L) + object + 1;
    if (dev != INVALID_DEVICE)
    {
	lua_getfield(L, LUA_REGISTRYINDEX, DEVPTR_REG);
	lua_pushlightuserdata(L, dev);
	lua_gettable(L, -2);
	if (!lua_isnil(L, -1))
	{
	    /* use existing object for this device */
	    lua_remove(L, -2);
	    udev = (libusb_device**)lua_touserdata(L, -1);
	    return udev;
	}
	lua_pop(L, 2);
    }
    udev = (libusb_device**)lua_newuserdata(L, sizeof(libusb_device*));
    *udev = dev;
    luaL_getmetatable(L, DEVICE_MT);
    lua_setmetatable(L, -2);
    if (dev != INVALID_DEVICE)
    {
	libusb_ref_device(dev);
	devptr(L, -1, dev);
    }
    /* associate device with context */
    lua_pushvalue(L, -1);
    lua_getmetatable(L, object);
    luaL_getmetatable(L, HANDLE_MT);
    if (lua_rawequal(L, -1, -2))
    {
	/* find context from handle */
	lua_pop(L, 2);
	lua_getfield(L, LUA_REGISTRYINDEX, HANDLES_REG);
	lua_pushvalue(L, object);
	lua_gettable(L, -2);
    }
    else
    {
	lua_pop(L, 2);
	lua_pushvalue(L, object);
    }
    lua_getmetatable(L, -1);
    luaL_getmetatable(L, CONTEXT_MT);
    if (lua_rawequal(L, -1, -2))
    {
	lua_pop(L, 2);
	lua_getfield(L, LUA_REGISTRYINDEX, DEVICES_REG);
	lua_insert(L, -3);
	lua_settable(L, -3);
	lua_pop(L, 1);
    }
    else
    {
	/* not a context object, ignore it */
	lua_pop(L, 4);
    }
    return udev;
}

static libusb_device_handle** newhandle(lua_State *L, int object)
{
    libusb_device_handle **handle;
    if (object < 0)
	object = lua_gettop(L) + object + 1;
    handle = (libusb_device_handle**)lua_newuserdata(L, sizeof(libusb_device_handle*));
    *handle = INVALID_HANDLE;
    luaL_getmetatable(L, HANDLE_MT);
    lua_setmetatable(L, -2);
    /* associate handle with context */
    lua_pushvalue(L, -1);
    lua_getmetatable(L, object);
    luaL_getmetatable(L, DEVICE_MT);
    if (lua_rawequal(L, -1, -2))
    {
	/* find context from device */
	lua_pop(L, 2);
	lua_getfield(L, LUA_REGISTRYINDEX, DEVICES_REG);
	lua_pushvalue(L, object);
	lua_gettable(L, -2);
    }
    else
    {
	lua_pop(L, 2);
	lua_pushvalue(L, object);
    }
    lua_getmetatable(L, -1);
    luaL_getmetatable(L, CONTEXT_MT);
    if (lua_rawequal(L, -1, -2))
    {
	lua_pop(L, 2);
	lua_getfield(L, LUA_REGISTRYINDEX, HANDLES_REG);
	lua_insert(L, -3);
	lua_settable(L, -3);
	lua_pop(L, 1);
    }
    else
    {
	/* not a context object, ignore it */
	lua_pop(L, 4);
    }
    return handle;
}

static int exitctx(lua_State *L)
{
    libusb_context **ud;
    ud = (libusb_context**)luaL_checkudata(L, 1, CONTEXT_MT);
    if (*ud != INVALID_CONTEXT)
    {
    	libusb_exit(*ud);
	*ud = INVALID_CONTEXT;
    }
    return 0;
}

static int unrefdev(lua_State *L)
{
    libusb_device **ud;
    ud = (libusb_device**)luaL_checkudata(L, 1, DEVICE_MT);
    if (*ud != INVALID_DEVICE)
    {
	libusb_unref_device(*ud);
	*ud = INVALID_DEVICE;
    }
    return 0;
}

static int closehandle(lua_State *L)
{
    libusb_device_handle **ud;
    ud = (libusb_device_handle**)luaL_checkudata(L, 1, HANDLE_MT);
    if (*ud != INVALID_HANDLE)
    {
	libusb_close(*ud);
	*ud = INVALID_HANDLE;
    }
    return 0;
}

static int freetransfer(lua_State *L)
{
    struct libusb_transfer **ud;
    ud = (struct libusb_transfer**)luaL_checkudata(L, 1, TRANSFER_MT);
    if (*ud != INVALID_TRANSFER)
    {
	libusb_free_transfer(*ud);
	*ud = INVALID_TRANSFER;
    }
    return 0;
}

static int pushdevicedesc(lua_State *L, const struct libusb_device_descriptor *desc)
{
    lua_createtable(L, 0, 14);
    lua_pushliteral(L, "bLength");
    lua_pushinteger(L, desc->bLength);
    lua_settable(L, -3);
    lua_pushliteral(L, "bDescriptorType");
    lua_pushinteger(L, desc->bDescriptorType);
    lua_settable(L, -3);
    lua_pushliteral(L, "bcdUSB");
    lua_pushinteger(L, desc->bcdUSB);
    lua_settable(L, -3);
    lua_pushliteral(L, "bDeviceClass");
    lua_pushinteger(L, desc->bDeviceClass);
    lua_settable(L, -3);
    lua_pushliteral(L, "bDeviceSubClass");
    lua_pushinteger(L, desc->bDeviceSubClass);
    lua_settable(L, -3);
    lua_pushliteral(L, "bDeviceProtocol");
    lua_pushinteger(L, desc->bDeviceProtocol);
    lua_settable(L, -3);
    lua_pushliteral(L, "bMaxPacketSize0");
    lua_pushinteger(L, desc->bMaxPacketSize0);
    lua_settable(L, -3);
    lua_pushliteral(L, "idVendor");
    lua_pushinteger(L, desc->idVendor);
    lua_settable(L, -3);
    lua_pushliteral(L, "idProduct");
    lua_pushinteger(L, desc->idProduct);
    lua_settable(L, -3);
    lua_pushliteral(L, "bcdDevice");
    lua_pushinteger(L, desc->bcdDevice);
    lua_settable(L, -3);
    lua_pushliteral(L, "iManufacturer");
    lua_pushinteger(L, desc->iManufacturer);
    lua_settable(L, -3);
    lua_pushliteral(L, "iProduct");
    lua_pushinteger(L, desc->iProduct);
    lua_settable(L, -3);
    lua_pushliteral(L, "iSerialNumber");
    lua_pushinteger(L, desc->iSerialNumber);
    lua_settable(L, -3);
    lua_pushliteral(L, "bNumConfigurations");
    lua_pushinteger(L, desc->bNumConfigurations);
    lua_settable(L, -3);
    return 1;
}

static int pushendpointdesc(lua_State *L, const struct libusb_endpoint_descriptor *desc)
{
    lua_createtable(L, 0, 8);
    lua_pushliteral(L, "bLength");
    lua_pushinteger(L, desc->bLength);
    lua_settable(L, -3);
    lua_pushliteral(L, "bDescriptorType");
    lua_pushinteger(L, desc->bDescriptorType);
    lua_settable(L, -3);
    lua_pushliteral(L, "bEndpointAddress");
    lua_pushinteger(L, desc->bEndpointAddress);
    lua_settable(L, -3);
    lua_pushliteral(L, "bmAttributes");
    lua_pushinteger(L, desc->bmAttributes);
    lua_settable(L, -3);
    lua_pushliteral(L, "wMaxPacketSize");
    lua_pushinteger(L, desc->wMaxPacketSize);
    lua_settable(L, -3);
    lua_pushliteral(L, "bInterval");
    lua_pushinteger(L, desc->bInterval);
    lua_settable(L, -3);
    if (desc->bLength >= LIBUSB_DT_ENDPOINT_AUDIO_SIZE)
    {
	lua_pushliteral(L, "bRefresh");
	lua_pushinteger(L, desc->bRefresh);
	lua_settable(L, -3);
	lua_pushliteral(L, "bSynchAddress");
	lua_pushinteger(L, desc->bSynchAddress);
	lua_settable(L, -3);
    }
    return 1;
}

static int pushinterfacedesc(lua_State *L, const struct libusb_interface_descriptor *desc, int includeendpoints)
{
    lua_createtable(L, 0, 9+includeendpoints);
    lua_pushliteral(L, "bLength");
    lua_pushinteger(L, desc->bLength);
    lua_settable(L, -3);
    lua_pushliteral(L, "bDescriptorType");
    lua_pushinteger(L, desc->bDescriptorType);
    lua_settable(L, -3);
    lua_pushliteral(L, "bInterfaceNumber");
    lua_pushinteger(L, desc->bInterfaceNumber);
    lua_settable(L, -3);
    lua_pushliteral(L, "bAlternateSetting");
    lua_pushinteger(L, desc->bAlternateSetting);
    lua_settable(L, -3);
    lua_pushliteral(L, "bNumEndpoints");
    lua_pushinteger(L, desc->bNumEndpoints);
    lua_settable(L, -3);
    lua_pushliteral(L, "bInterfaceClass");
    lua_pushinteger(L, desc->bInterfaceClass);
    lua_settable(L, -3);
    lua_pushliteral(L, "bInterfaceSubClass");
    lua_pushinteger(L, desc->bInterfaceSubClass);
    lua_settable(L, -3);
    lua_pushliteral(L, "bInterfaceProtocol");
    lua_pushinteger(L, desc->bInterfaceProtocol);
    lua_settable(L, -3);
    lua_pushliteral(L, "iInterface");
    lua_pushinteger(L, desc->iInterface);
    lua_settable(L, -3);
    if (includeendpoints)
    {
	unsigned int i;
	lua_pushliteral(L, "endpoint");
	lua_createtable(L, desc->bNumEndpoints, 0);
	for (i = 0; i < desc->bNumEndpoints; ++i)
	{
	    pushendpointdesc(L, &desc->endpoint[i]);
	    lua_rawseti(L, -2, i+1);
	}
	lua_settable(L, -3);
    }
    return 1;
}

static int pushconfigdesc(lua_State *L, const struct libusb_config_descriptor *desc)
{
    unsigned int i,j;
    lua_createtable(L, 0, 9);
    lua_pushliteral(L, "bLength");
    lua_pushinteger(L, desc->bLength);
    lua_settable(L, -3);
    lua_pushliteral(L, "bDescriptorType");
    lua_pushinteger(L, desc->bDescriptorType);
    lua_settable(L, -3);
    lua_pushliteral(L, "wTotalLength");
    lua_pushinteger(L, desc->wTotalLength);
    lua_settable(L, -3);
    lua_pushliteral(L, "bNumInterfaces");
    lua_pushinteger(L, desc->bNumInterfaces);
    lua_settable(L, -3);
    lua_pushliteral(L, "bConfigurationValue");
    lua_pushinteger(L, desc->bConfigurationValue);
    lua_settable(L, -3);
    lua_pushliteral(L, "iConfiguration");
    lua_pushinteger(L, desc->iConfiguration);
    lua_settable(L, -3);
    lua_pushliteral(L, "bmAttributes");
    lua_pushinteger(L, desc->bmAttributes);
    lua_settable(L, -3);
    lua_pushliteral(L, "MaxPower");
    lua_pushinteger(L, desc->MaxPower);
    lua_settable(L, -3);
    lua_pushliteral(L, "interface");
    lua_createtable(L, desc->bNumInterfaces, 0);
    for (i = 0; i < desc->bNumInterfaces; ++i)
    {
	lua_createtable(L, desc->interface[i].num_altsetting, 0);
	for (j = 0; j < desc->interface[i].num_altsetting; ++j)
	{
	    pushinterfacedesc(L, desc->interface[i].altsetting+j, 1);
	    lua_rawseti(L, -2, j+1);
	}
	lua_rawseti(L, -2, i+1);
    }
    lua_settable(L, -3);
    return 1;
}

static libusb_context* getctx(lua_State *L, int ix)
{
    libusb_context **ctx;
    ctx = (libusb_context**)luaL_checkudata(L, ix, CONTEXT_MT);
    if (ctx == NULL || *ctx == INVALID_CONTEXT)
	luaL_error(L, "attempt to use an invalid context");
    return *ctx;
}

static libusb_device* getdev(lua_State *L, int ix)
{
    libusb_device **dev;
    dev = (libusb_device**)luaL_checkudata(L, ix, DEVICE_MT);
    if (dev == NULL || *dev == INVALID_DEVICE)
	luaL_error(L, "attempt to use an invalid device");
    return *dev;
}

static libusb_device_handle* gethandle(lua_State *L, int ix)
{
    libusb_device_handle **handle;
    handle = (libusb_device_handle**)luaL_checkudata(L, ix, HANDLE_MT);
    if (handle == NULL || *handle == INVALID_HANDLE)
	luaL_error(L, "attempt to use a closed device");
    return *handle;
}

static struct libusb_transfer* gettransfer(lua_State *L, int ix)
{
    struct libusb_transfer **transfer;
    transfer = (struct libusb_transfer**)luaL_checkudata(L, ix, TRANSFER_MT);
    if (*transfer == INVALID_TRANSFER)
	luaL_error(L, "attempt to use an invalid transfer");
    return *transfer;
}

static int lusb_init(lua_State *L)
{
    int err;
    libusb_context **ctx = newctx(L);
    if ((err = libusb_init(ctx)) != 0)
	return _err(L, err);
    return 1;
}

static int lusb_set_debug(lua_State *L)
{
    libusb_context *ctx;
    int level;
    lua_settop(L, 2);
    if (lua_isuserdata(L, 1))
    {
    	ctx = getctx(L, 1);
	level = luaL_checkinteger(L, 2);
    }
    else
    {
	ctx = defctx(L);
	level = luaL_checkinteger(L, 1);
    }
    libusb_set_debug(ctx, level);
    return 0;
}

static int lusb_get_device_list(lua_State *L)
{
    libusb_context *ctx;
    libusb_device **devlist;
    ssize_t numdevices, n;
    lua_settop(L, 1);
    if (lua_isuserdata(L, 1))
    {
    	ctx = getctx(L, 1);
    }
    else
    {
	lua_settop(L, 0);
	ctx = defctx(L);
    }
    numdevices = libusb_get_device_list(ctx, &devlist);
    if (numdevices < 0)
	return _err(L, numdevices);
    lua_createtable(L, numdevices, 0);
    for (n = 0; n < numdevices; ++n)
    {
	newdev(L, 1, devlist[n]);
	lua_rawseti(L, -2, n+1);
    }
    libusb_free_device_list(devlist, 1);
    return 1;
}

static int lusb_get_bus_number(lua_State *L)
{
    libusb_device *dev = getdev(L, 1);
    lua_pushinteger(L, libusb_get_bus_number(dev));
    return 1;
}

static int lusb_get_device_address(lua_State *L)
{
    libusb_device *dev = getdev(L, 1);
    lua_pushinteger(L, libusb_get_device_address(dev));
    return 1;
}

static int lusb_get_max_packet_size(lua_State *L)
{
    libusb_device *dev = getdev(L, 1);
    int endp = luaL_checkinteger(L, 2);
    int num = libusb_get_max_packet_size(dev, endp);
    if (num < 0)
	return _err(L, num);
    lua_pushinteger(L, num);
    return 1;
}

static int lusb_get_max_iso_packet_size(lua_State *L)
{
    libusb_device *dev = getdev(L, 1);
    int endp = luaL_checkinteger(L, 2);
    int num = libusb_get_max_iso_packet_size(dev, endp);
    if (num < 0)
	return _err(L, num);
    lua_pushinteger(L, num);
    return 1;
}

static int lusb_get_device_descriptor(lua_State *L)
{
    struct libusb_device_descriptor desc;
    libusb_device *dev;
    int err;
    dev = getdev(L, 1);
    if ((err = libusb_get_device_descriptor(dev, &desc)) != 0)
	return _err(L, err);
    pushdevicedesc(L, &desc);
    return 1;
}

static int lusb_get_active_config_descriptor(lua_State *L)
{
    struct libusb_config_descriptor *desc;
    libusb_device *dev;
    int err;
    dev = getdev(L, 1);
    if ((err = libusb_get_active_config_descriptor(dev, &desc)) != 0)
	return _err(L, err);
    pushconfigdesc(L, desc);
    libusb_free_config_descriptor(desc);
    return 1;
}

static int lusb_get_config_descriptor(lua_State *L)
{
    struct libusb_config_descriptor *desc;
    libusb_device *dev;
    int idx, err;
    dev = getdev(L, 1);
    idx = luaL_checkinteger(L, 2);
    if ((err = libusb_get_config_descriptor(dev, idx, &desc)) != 0)
	return _err(L, err);
    pushconfigdesc(L, desc);
    libusb_free_config_descriptor(desc);
    return 1;
}

static int lusb_get_config_descriptor_by_value(lua_State *L)
{
    struct libusb_config_descriptor *desc;
    libusb_device *dev;
    int val, err;
    dev = getdev(L, 1);
    val = luaL_checkinteger(L, 2);
    if ((err = libusb_get_config_descriptor_by_value(dev, val, &desc)) != 0)
	return _err(L, err);
    pushconfigdesc(L, desc);
    libusb_free_config_descriptor(desc);
    return 1;
}

static int lusb_get_descriptor(lua_State *L)
{
    libusb_device_handle *handle;
    unsigned char buf[255];
    struct usb_descriptor *desc;
    int dtype, idx;
    int err;
    handle = gethandle(L, 1);
    dtype = luaL_checkinteger(L, 2);
    idx = luaL_checkinteger(L, 3);
    err = libusb_get_descriptor(handle, dtype, idx, buf, sizeof(buf));
    if (err < 0)
	return _err(L, err);
    desc = (struct usb_descriptor*)buf;
    switch (desc->bDescriptorType)
    {
    case LIBUSB_DT_DEVICE:
	pushdevicedesc(L, (struct libusb_device_descriptor*)desc);
	break;
    case LIBUSB_DT_INTERFACE:
	pushinterfacedesc(L, (struct libusb_interface_descriptor*)desc, 0);
	break;
    case LIBUSB_DT_ENDPOINT:
	pushendpointdesc(L, (struct libusb_endpoint_descriptor*)desc);
	break;
    case LIBUSB_DT_STRING:
	lua_pushlstring(L, (char*)((struct usb_string_descriptor*)desc)->bString, desc->bLength-2);
	break;
    default:
	lua_pushlstring(L, (char*)buf, desc->bLength);
	break;
    }
    return 1;
}

static int lusb_get_string_descriptor(lua_State *L)
{
    struct usb_string_descriptor desc;
    libusb_device_handle *handle;
    int idx, langid;
    size_t len;
    int err;
    handle = gethandle(L, 1);
    idx = luaL_checkinteger(L, 2);
    langid = luaL_optinteger(L, 3, 0);
    len = sizeof(struct usb_string_descriptor);
    err = libusb_get_string_descriptor(handle, idx, langid, (unsigned char*)&desc, len);
    if (err < 0)
	return _err(L, err);
    len = desc.bLength - 2;
    if (idx == 0)
    {
	/* descriptor is array of langids */
	int i;
	len /= 2;
	lua_createtable(L, len, 0);
	for (i = 0; i < len; ++i)
	{
	    lua_pushinteger(L, desc.bString[i]);
	    lua_rawseti(L, -2, i+1);
	}
	return 1;
    }
    lua_pushlstring(L, (const char*)desc.bString, len);
    return 1;
}

static int lusb_get_string_descriptor_ascii(lua_State *L)
{
    libusb_device_handle *handle;
    unsigned char str[255];
    int idx;
    int err;
    handle = gethandle(L, 1);
    idx = luaL_checkinteger(L, 2);
    if (idx == 0)
	return _err(L, LIBUSB_ERROR_INVALID_PARAM);
    err = libusb_get_string_descriptor_ascii(handle, idx, str, sizeof(str));
    if (err < 0)
	return _err(L, err);
    lua_pushstring(L, (const char*)str);
    return 1;
}

static int lusb_get_string_descriptor_utf8(lua_State *L)
{
    struct usb_string_descriptor desc;
    libusb_device_handle *handle;
    int idx, langid;
    size_t len, i;
    int err;
    luaL_Buffer buffer;
    handle = gethandle(L, 1);
    idx = luaL_checkinteger(L, 2);
    if (idx == 0)
	return _err(L, LIBUSB_ERROR_INVALID_PARAM);
    len = sizeof(struct usb_string_descriptor);
    err = libusb_get_string_descriptor(handle, 0, 0, (unsigned char*)&desc, len);
    if (err < 0)
	return _err(L, err);
    if (err < 4)
	return _err(L, LIBUSB_ERROR_IO);
    langid = desc.bString[0];
    err = libusb_get_string_descriptor(handle, idx, langid, (unsigned char*)&desc, len);
    if (err < 0)
	return _err(L, err);
    len = (desc.bLength - 2)/2;
    luaL_buffinit(L, &buffer);
    for (i = 0; i < len; ++i)
    {
	unsigned int ch = desc.bString[i];
	if (ch < 0x80)
	{
	    luaL_addchar(&buffer, ch);
	}
	else if (ch < 0x800)
	{
	    luaL_addchar(&buffer, (ch>>6)|0xC0);
	    luaL_addchar(&buffer, (ch&0x3F)|0x80);
	}
	else if ((ch&0xF800) == 0xD800)
	{
	    unsigned int ch2 = desc.bString[i+1];
	    if ((ch2&0xFC00) == 0xDC00)
	    {
		ch = (((ch&0x3FF)<<10)|(ch2&0x3FF)) + 0x10000;
		luaL_addchar(&buffer, (ch>>18)|0xF0);
		luaL_addchar(&buffer, ((ch>>12)&0x3F)|0x80);
		luaL_addchar(&buffer, ((ch>>6)&0x3F)|0x80);
		luaL_addchar(&buffer, (ch&0x3F)|0x80);
	    }
	    else
	    {
		luaL_addchar(&buffer, 0xEF);
		luaL_addchar(&buffer, 0xBF);
		luaL_addchar(&buffer, 0xBD);
	    }
	}
	else
	{
	    luaL_addchar(&buffer, (ch>>12)|0xE0);
	    luaL_addchar(&buffer, ((ch>>6)&0x3F)|0x80);
	    luaL_addchar(&buffer, (ch&0x3F)|0x80);
	}
    }
    luaL_pushresult(&buffer);
    return 1;
}

static int lusb_open_device_with_vid_pid(lua_State *L)
{
    libusb_context *ctx;
    libusb_device_handle **ud, *handle;
    uint16_t vid, pid;
    lua_settop(L, 3);
    if (lua_isuserdata(L, 1))
    {
    	ctx = getctx(L, 1);
    }
    else
    {
	ctx = defctx(L);
	lua_insert(L, 1);
    }
    vid = luaL_checkinteger(L, 2);
    pid = luaL_checkinteger(L, 3);
    ud = newhandle(L, 1);
    handle = libusb_open_device_with_vid_pid(ctx, vid, pid);
    /* the real error code is lost, just return NO_DEVICE every time */
    if (handle == NULL)
	return _err(L, LIBUSB_ERROR_NO_DEVICE);
    *ud = handle;
    return 1;
}

static int lusb_open(lua_State *L)
{
    libusb_device *dev;
    libusb_device_handle **handle;
    int err;
    dev = getdev(L, 1);
    handle = newhandle(L, 1);
    if ((err = libusb_open(dev, handle)) != 0)
	return _err(L, err);
    return 1;
}

static int lusb_get_device(lua_State *L)
{
    libusb_device_handle *handle = gethandle(L, 1);
    libusb_device *dev = libusb_get_device(handle);
    newdev(L, 1, dev);
    return 1;
}

static int lusb_get_configuration(lua_State *L)
{
    libusb_device_handle *handle = gethandle(L, 1);
    int cfg, err;
    if ((err = libusb_get_configuration(handle, &cfg)) != 0)
	return _err(L, err);
    lua_pushinteger(L, cfg);
    return 1;
}

static int lusb_set_configuration(lua_State *L)
{
    libusb_device_handle *handle;
    int cfg, err;
    handle = gethandle(L, 1);
    cfg = luaL_checkinteger(L, 2);
    if ((err = libusb_set_configuration(handle, cfg)) != 0)
	return _err(L, err);
    lua_settop(L, 1);
    return 1;
}

static int lusb_claim_interface(lua_State *L)
{
    libusb_device_handle *handle;
    int iface, err;
    handle = gethandle(L, 1);
    iface = luaL_checkinteger(L, 2);
    if ((err = libusb_claim_interface(handle, iface)) != 0)
	return _err(L, err);
    lua_settop(L, 1);
    return 1;
}

static int lusb_release_interface(lua_State *L)
{
    libusb_device_handle *handle;
    int iface, err;
    handle = gethandle(L, 1);
    iface = luaL_checkinteger(L, 2);
    if ((err = libusb_release_interface(handle, iface)) != 0)
	return _err(L, err);
    lua_settop(L, 1);
    return 1;
}

static int lusb_set_interface_alt_setting(lua_State *L)
{
    libusb_device_handle *handle;
    int iface, alt, err;
    handle = gethandle(L, 1);
    iface = luaL_checkinteger(L, 2);
    alt = luaL_checkinteger(L, 3);
    if ((err = libusb_set_interface_alt_setting(handle, iface, alt)) != 0)
	return _err(L, err);
    lua_settop(L, 1);
    return 1;
}

static int lusb_clear_halt(lua_State *L)
{
    libusb_device_handle *handle;
    int endp, err;
    handle = gethandle(L, 1);
    endp = luaL_checkinteger(L, 2);
    if ((err = libusb_clear_halt(handle, endp)) != 0)
	return _err(L, err);
    lua_settop(L, 1);
    return 1;
}

static int lusb_reset_device(lua_State *L)
{
    libusb_device_handle *handle;
    int err;
    handle = gethandle(L, 1);
    if ((err = libusb_reset_device(handle)) != 0)
	return _err(L, err);
    lua_settop(L, 1);
    return 1;
}

static int lusb_kernel_driver_active(lua_State *L)
{
    libusb_device_handle *handle;
    int iface, err;
    handle = gethandle(L, 1);
    iface = luaL_checkinteger(L, 2);
    if ((err = libusb_kernel_driver_active(handle, iface)) < 0)
	return _err(L, err);
    lua_pushboolean(L, err);
    return 1;
}

static int lusb_detach_kernel_driver(lua_State *L)
{
    libusb_device_handle *handle;
    int iface, err;
    handle = gethandle(L, 1);
    iface = luaL_checkinteger(L, 2);
    if ((err = libusb_detach_kernel_driver(handle, iface)) != 0)
	return _err(L, err);
    lua_settop(L, 1);
    return 1;
}

static int lusb_attach_kernel_driver(lua_State *L)
{
    libusb_device_handle *handle;
    int iface, err;
    handle = gethandle(L, 1);
    iface = luaL_checkinteger(L, 2);
    if ((err = libusb_attach_kernel_driver(handle, iface)) != 0)
	return _err(L, err);
    lua_settop(L, 1);
    return 1;
}

static int lusb_control_transfer(lua_State *L)
{
    luaL_Buffer buffer;
    libusb_device_handle *handle;
    int reqt, req, err;
    uint16_t val, idx;
    unsigned char *data;
    size_t len;
    unsigned int timeout;
    lua_settop(L, 7);
    handle = gethandle(L, 1);
    reqt = luaL_checkinteger(L, 2);
    req = luaL_checkinteger(L, 3);
    val = luaL_checkinteger(L, 4);
    idx = luaL_checkinteger(L, 5);
    timeout = luaL_optunsigned(L, 7, 0);
    if ((reqt & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN)
    {
	len = luaL_checkunsigned(L, 6);
	luaL_buffinit(L, &buffer);
	data = (unsigned char*)luaL_prepbuffsize(&buffer, len);
	err = libusb_control_transfer(handle, reqt, req, val, idx,
				      data, len, timeout);
	if (err < 0)
	    return _err(L, err);
	luaL_addbuffsize(&buffer, err);
	luaL_pushresult(&buffer);
	return 1;
    }
    else /* LIBUSB_ENDPOINT_OUT */
    {
	data = (unsigned char*)luaL_checklstring(L, 6, &len);
	err = libusb_control_transfer(handle, reqt, req, val, idx,
				      data, len, timeout);
	if (err < 0)
	    return _err(L, err);
	lua_pushinteger(L, err);
	return 1;
    }
    return 0; /* make compiler happy */
}

static int lusb_bulk_transfer(lua_State *L)
{
    luaL_Buffer buffer;
    libusb_device_handle *handle;
    int endp, err;
    unsigned char *data;
    size_t len;
    unsigned int timeout;
    lua_settop(L, 4);
    handle = gethandle(L, 1);
    endp = luaL_checkinteger(L, 2);
    timeout = luaL_optunsigned(L, 4, 0);
    if ((endp & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN)
    {
	len = luaL_checkunsigned(L, 3);
	luaL_buffinit(L, &buffer);
	data = (unsigned char*)luaL_prepbuffsize(&buffer, len);
	err = libusb_bulk_transfer(handle, endp, data, len,
				   (int*)&len, timeout);
	if (err < 0 && err != LIBUSB_ERROR_TIMEOUT)
	    return _err(L, err);
	luaL_addbuffsize(&buffer, len);
	luaL_pushresult(&buffer);
	lua_pushboolean(L, err == LIBUSB_ERROR_TIMEOUT);
	return 2;
    }
    else /* LIBUSB_ENDPOINT_OUT */
    {
	data = (unsigned char*)luaL_checklstring(L, 6, &len);
	err = libusb_bulk_transfer(handle, endp, data, len,
				   (int*)&len, timeout);
	if (err < 0 && err != LIBUSB_ERROR_TIMEOUT)
	    return _err(L, err);
	lua_pushinteger(L, len);
	lua_pushboolean(L, err == LIBUSB_ERROR_TIMEOUT);
	return 2;
    }
    return 0; /* make compiler happy */
}

static int lusb_interrupt_transfer(lua_State *L)
{
    luaL_Buffer buffer;
    libusb_device_handle *handle;
    int endp, err;
    unsigned char *data;
    size_t len;
    unsigned int timeout;
    lua_settop(L, 4);
    handle = gethandle(L, 1);
    endp = luaL_checkinteger(L, 2);
    timeout = luaL_optunsigned(L, 4, 0);
    if ((endp & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN)
    {
	len = luaL_checkunsigned(L, 3);
	luaL_buffinit(L, &buffer);
	data = (unsigned char*)luaL_prepbuffsize(&buffer, len);
	err = libusb_interrupt_transfer(handle, endp, data, len,
					(int*)&len, timeout);
	if (err < 0 && err != LIBUSB_ERROR_TIMEOUT)
	    return _err(L, err);
	luaL_addbuffsize(&buffer, len);
	luaL_pushresult(&buffer);
	lua_pushboolean(L, err == LIBUSB_ERROR_TIMEOUT);
	return 2;
    }
    else /* LIBUSB_ENDPOINT_OUT */
    {
	data = (unsigned char*)luaL_checklstring(L, 6, &len);
	err = libusb_interrupt_transfer(handle, endp, data, len,
					(int*)&len, timeout);
	if (err < 0 && err != LIBUSB_ERROR_TIMEOUT)
	    return _err(L, err);
	lua_pushinteger(L, len);
	lua_pushboolean(L, err == LIBUSB_ERROR_TIMEOUT);
	return 2;
    }
    return 0; /* make compiler happy */
}

struct lusb_transfer_cb_ud
{
    lua_State *L;
    int ref;
};

static void lusb_transfer_cb_fn(struct libusb_transfer *tx)
{
    lua_State *L = ((struct lusb_transfer_cb_ud*)tx->user_data)->L;
    lua_getfield(L, LUA_REGISTRYINDEX, TRANSFER_REG);
    lua_rawgeti(L, -1, ((struct lusb_transfer_cb_ud*)tx->user_data)->ref);
    if (!lua_isnil(L, -1))
    {
	/* { callback, transfer, ud } */
	lua_rawgeti(L, -1, 1);
	if (!lua_isnil(L, -1))
	{
	    lua_rawgeti(L, -2, 2);
	    lua_pushinteger(L, tx->status);
	    lua_pushinteger(L, tx->actual_length);
	    lua_pcall(L, 3, 0, 0);
	}
	else
	{
	    /* no callback */
	    lua_pop(L, 1);
	}
    }
    lua_pop(L, 1);
    lua_pushnil(L);
    lua_rawgeti(L, -2, ((struct lusb_transfer_cb_ud*)tx->user_data)->ref);
    lua_pop(L, 1);
}

static struct lusb_transfer_cb_ud* callback(lua_State *L, int tx, int cb)
{
    struct lusb_transfer_cb_ud *ud;
    lua_getfield(L, LUA_REGISTRYINDEX, TRANSFER_REG);
    lua_createtable(L, 3, 0);
    ud = (struct lusb_transfer_cb_ud*)lua_newuserdata(L, sizeof(struct lusb_transfer_cb_ud));
    lua_pushvalue(L, tx);
    lua_pushvalue(L, cb);
    lua_rawseti(L, -4, 1);
    lua_rawseti(L, -3, 2);
    lua_rawseti(L, -2, 3);
    ud->ref = lua_rawlen(L, -2) + 1;
    ud->L = L;
    lua_rawseti(L, -2, ud->ref);
    lua_pop(L, 1);
    return ud;
}

static int lusb_transfer(lua_State *L)
{
    struct libusb_transfer **tx;
    int num = luaL_optinteger(L, 1, 0);
    tx = (struct libusb_transfer**)lua_newuserdata(L, sizeof(struct libusb_transfer*));
    *tx = INVALID_TRANSFER;
    luaL_getmetatable(L, TRANSFER_MT);
    lua_setmetatable(L, -2);
    *tx = libusb_alloc_transfer(num);
    if (*tx == NULL)
	return _err(L, LIBUSB_ERROR_NO_MEM);
    return 1;
}

static int lusb_submit_transfer(lua_State *L)
{
    struct libusb_transfer *tx;
    int err;
    lua_settop(L, 3);
    tx = gettransfer(L, 1);
    tx->timeout = luaL_optunsigned(L, 3, 0);
    tx->user_data = callback(L, 1, 2);
    tx->callback = lusb_transfer_cb_fn;
    err = libusb_submit_transfer(tx);
    return _err(L, err);
}

static int lusb_cancel_transfer(lua_State *L)
{
    struct libusb_transfer *tx;
    int err;
    tx = gettransfer(L, 1);
    err = libusb_cancel_transfer(tx);
    return _err(L, err);
}

static int lusb_transfer_get_data(lua_State *L)
{
    struct libusb_transfer *tx;
    tx = gettransfer(L, 1);
    if (tx->buffer == NULL)
    {
	lua_pushnil(L);
	return 1;
    }
    lua_pushlstring(L, (char*)tx->buffer, tx->actual_length);
    return 1;
}

static int lusb_control_transfer_get_data(lua_State *L)
{
    struct libusb_transfer *tx;
    struct libusb_control_setup *setup;
    tx = gettransfer(L, 1);
    if (tx->buffer == NULL)
    {
	lua_pushnil(L);
	return 1;
    }
    /*setup = libusb_control_transfer_get_setup(tx);*/
    lua_pushlstring(L, (char*)libusb_control_transfer_get_data(tx), tx->actual_length);
    return 1;
}

static int lusb_control_transfer_get_setup(lua_State *L)
{
    struct libusb_transfer *tx;
    struct libusb_control_setup *setup;
    tx = gettransfer(L, 1);
    if (tx->buffer == NULL)
    {
	lua_pushnil(L);
	return 1;
    }
    setup = libusb_control_transfer_get_setup(tx);
    lua_createtable(L, 5, 0);
    lua_pushliteral(L, "bmRequestType");
    lua_pushinteger(L, setup->bmRequestType);
    lua_settable(L, -3);
    lua_pushliteral(L, "bRequest");
    lua_pushinteger(L, setup->bRequest);
    lua_settable(L, -3);
    lua_pushliteral(L, "wValue");
    lua_pushinteger(L, setup->wValue);
    lua_settable(L, -3);
    lua_pushliteral(L, "wIndex");
    lua_pushinteger(L, setup->wIndex);
    lua_settable(L, -3);
    lua_pushliteral(L, "wLength");
    lua_pushinteger(L, setup->wLength);
    lua_settable(L, -3);
    return 1;
}

static unsigned char* transferbuffer(lua_State *L, int transferidx, int len)
{
    unsigned char *buf;
    struct libusb_transfer *tx;
    tx = *(struct libusb_transfer**)lua_touserdata(L, transferidx);
    buf = (unsigned char*)lua_newuserdata(L, len);
    lua_getfield(L, LUA_REGISTRYINDEX, BUFFER_REG);
    lua_pushvalue(L, transferidx);
    lua_pushvalue(L, -2);
    lua_settable(L, -3);
    tx->buffer = buf;
    tx->length = len;
    tx->actual_length = 0;
    return buf;
}

static unsigned char* controlsetuptable(lua_State *L, int obj,
		      int *reqt, int *req, int *val, int *idx,
		      size_t *len, unsigned char *data)
{
    lua_pushliteral(L, "bmRequestType");
    lua_gettable(L, obj);
    if (lua_isnumber(L, -1))
	*reqt = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_pushliteral(L, "bRequest");
    lua_gettable(L, obj);
    if (lua_isnumber(L, -1))
	*req = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_pushliteral(L, "wValue");
    lua_gettable(L, obj);
    if (lua_isnumber(L, -1))
	*val = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_pushliteral(L, "wIndex");
    lua_gettable(L, obj);
    if (lua_isnumber(L, -1))
	*idx = lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (len)
    {
	lua_rawgeti(L, obj, 1);
	if (lua_isstring(L, -1))
	{
	    const char *str = lua_tolstring(L, -1, len);
	    lua_pop(L, 1);
	    return (unsigned char*)str;
	}
	lua_pop(L, 1);
	lua_pushliteral(L, "wLength");
	lua_gettable(L, obj);
	if (lua_isnumber(L, -1))
	{
	    *len = lua_tounsigned(L, -1);
	    data = NULL;
	}
	lua_pop(L, 1);
    }
    return data;
}

static void fillcontrolsetup(lua_State *L, int transferidx, int setupidx)
{
    struct libusb_transfer *tx;
    struct libusb_control_setup *setup;
    int reqt, req, val, idx;
    unsigned char *buf, *data, *olddata;
    size_t len, oldlen;
    tx = *(struct libusb_transfer**)lua_touserdata(L, transferidx);
    if (tx->buffer != NULL)
    {
	setup = libusb_control_transfer_get_setup(tx);
	reqt = setup->bmRequestType;
	req = setup->bRequest;
	val = setup->wValue;
	idx = setup->wIndex;
	len = oldlen = setup->wLength;
	data = olddata = libusb_control_transfer_get_data(tx);
    }
    else
    {
	reqt = req = val = idx = 0;
	len = oldlen = 0;
	data = olddata = NULL;
    }
    switch (lua_gettop(L) - setupidx)
    {
    case 0:
	if (lua_istable(L, setupidx))
	    data = controlsetuptable(L, setupidx, &reqt, &req, &val, &idx, &len, olddata);
	else if (lua_isnumber(L, setupidx))
	{
	    len = lua_tounsigned(L, setupidx);
	    data = NULL;
	}
	else
	    data = (unsigned char*)luaL_checklstring(L, setupidx, &len);
	break;
    case 1:
    default:
	controlsetuptable(L, setupidx, &reqt, &req, &val, &idx, NULL, NULL);
	if (lua_isnumber(L, setupidx+1))
	{
	    len = lua_tounsigned(L, setupidx+1);
	    data = NULL;
	}
	else
	    data = (unsigned char*)luaL_checklstring(L, setupidx+1, &len);
	break;
    }
    if (olddata != NULL && data == olddata)
    {
	libusb_fill_control_setup(tx->buffer, reqt, req, val, idx, oldlen);
    }
    else
    {
	buf = transferbuffer(L, transferidx, LIBUSB_CONTROL_SETUP_SIZE+len);
	libusb_fill_control_setup(buf, reqt, req, val, idx, len);
	if (data != NULL)
	    memcpy(buf+LIBUSB_CONTROL_SETUP_SIZE, data, len);
	else
	    memset(buf+LIBUSB_CONTROL_SETUP_SIZE, 0, len);
    }
}

static int lusb_fill_control_setup(lua_State *L)
{
    struct libusb_transfer *tx;
    tx = gettransfer(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    fillcontrolsetup(L, 1, 2);
    lua_settop(L, 1);
    return 1;
}

static int lusb_fill_control_transfer(lua_State *L)
{
    struct libusb_transfer *tx;
    libusb_device_handle *handle;
    tx = gettransfer(L, 1);
    handle = gethandle(L, 2);
    if (lua_gettop(L) > 2)
	fillcontrolsetup(L, 1, 3);
    libusb_fill_control_transfer(tx, handle, tx->buffer, NULL, NULL, 0);
    lua_settop(L, 1);
    return 1;
}

static int lusb_fill_bulk_transfer(lua_State *L)
{
    struct libusb_transfer *tx;
    libusb_device_handle *handle;
    int endp;
    unsigned char *buf, *data;
    size_t len;
    lua_settop(L, 4);
    tx = gettransfer(L, 1);
    handle = gethandle(L, 2);
    endp = luaL_checkinteger(L, 3);
    if (lua_isnumber(L, 4))
    {
	len = lua_tounsigned(L, 4);
	buf = transferbuffer(L, 1, len);
    }
    else
    {
	data = (unsigned char*)luaL_checklstring(L, 4, &len);
	buf = transferbuffer(L, 1, len>0?len:1);
	memcpy(buf, data, len);
    }
    libusb_fill_bulk_transfer(tx, handle, endp, buf, len, NULL, NULL, 0);
    lua_settop(L, 1);
    return 1;
}

static int lusb_fill_interrupt_transfer(lua_State *L)
{
    struct libusb_transfer *tx;
    libusb_device_handle *handle;
    int endp;
    unsigned char *buf, *data;
    size_t len;
    lua_settop(L, 4);
    tx = gettransfer(L, 1);
    handle = gethandle(L, 2);
    endp = luaL_checkinteger(L, 3);
    if (lua_isnumber(L, 4))
    {
	len = lua_tounsigned(L, 4);
	buf = transferbuffer(L, 1, len);
    }
    else
    {
	data = (unsigned char*)luaL_checklstring(L, 4, &len);
	buf = transferbuffer(L, 1, len>0?len:1);
	memcpy(buf, data, len);
    }
    libusb_fill_interrupt_transfer(tx, handle, endp, buf, len, NULL, NULL, 0);
    lua_settop(L, 1);
    return 1;
}

static int lusb_fill_iso_transfer(lua_State *L)
{
    struct libusb_transfer *tx;
    libusb_device_handle *handle;
    int endp, num;
    unsigned char *buf, *data;
    size_t len;
    lua_settop(L, 5);
    tx = gettransfer(L, 1);
    handle = gethandle(L, 2);
    endp = luaL_checkinteger(L, 3);
    num = luaL_checkinteger(L, 5);
    if (lua_isnumber(L, 4))
    {
	len = lua_tounsigned(L, 4);
	buf = transferbuffer(L, 1, len);
    }
    else
    {
	data = (unsigned char*)luaL_checklstring(L, 4, &len);
	buf = transferbuffer(L, 1, len>0?len:1);
	memcpy(buf, data, len);
    }
    libusb_fill_iso_transfer(tx, handle, endp, buf, len, num, NULL, NULL, 0);
    lua_settop(L, 1);
    return 1;
}

static int lusb_set_iso_packet_lengths(lua_State *L)
{
    struct libusb_transfer *tx;
    unsigned int len, num, i;
    lua_settop(L, 2);
    tx = gettransfer(L, 1);
    if (lua_istable(L, 2))
    {
	num = luaL_len(L, 2);
	if (num > tx->num_iso_packets)
	    num = tx->num_iso_packets;
	for (i = 0; i < num; ++i)
	{
	    lua_pushinteger(L, i+1);
	    lua_gettable(L, 2);
	    tx->iso_packet_desc[i].length = l_checkunsigned(L, 2, -1);
	    tx->iso_packet_desc[i].actual_length = 0;
	    lua_pop(L, 1);
	}
    }
    else
    {
	len = luaL_checkunsigned(L, 2);
	libusb_set_iso_packet_lengths(tx, len);
    }
    lua_settop(L, 1);
    return 1;
}

static int lusb_get_iso_packet_buffer(lua_State *L)
{
    struct libusb_transfer *tx;
    unsigned int i;
    unsigned char *data;
    ssize_t len;
    tx = gettransfer(L, 1);
    i = luaL_checkunsigned(L, 2);
    if (tx->buffer == NULL)
    {
	lua_pushnil(L);
	return 1;
    }
    data = libusb_get_iso_packet_buffer(tx, i);
    if (data == NULL)
    {
	lua_pushnil(L);
	return 1;
    }
    len = tx->length - (data - tx->buffer);
    if (tx->iso_packet_desc[i].actual_length < len)
	len = tx->iso_packet_desc[i].actual_length;
    else
	if (len < 0)
	    len = 0;
    lua_pushlstring(L, (char*)data, len);
    lua_pushinteger(L, tx->iso_packet_desc[i].status);
    return 2;
}

static int lusb_set_iso_packet_buffer(lua_State *L)
{
    struct libusb_transfer *tx;
    unsigned int i;
    unsigned char *data, *buf;
    size_t len;
    ssize_t buflen;
    tx = gettransfer(L, 1);
    i = luaL_checkunsigned(L, 2);
    data = (unsigned char*)luaL_checklstring(L, 3, &len);
    if (tx->buffer == NULL)
	luaL_error(L, "transfer buffer has not been created");
    buf = libusb_get_iso_packet_buffer(tx, i);
    if (buf == NULL)
	luaL_error(L, "invalid packet number");
    buflen = tx->length - (buf - tx->buffer);
    if (tx->iso_packet_desc[i].length < buflen)
	buflen = tx->iso_packet_desc[i].length;
    else
    {
	if (buflen < 0)
	    buflen = 0;
	tx->iso_packet_desc[i].length = buflen;
    }
    if (len > buflen)
	len = buflen;
    memcpy(buf, data, len);
    tx->iso_packet_desc[i].actual_length = len;
    lua_settop(L, 1);
    return 1;
}

static void pushtimeval(lua_State *L, struct timeval *tv)
{
    lua_Number ftime = (lua_Number)tv->tv_usec / 1000000 + tv->tv_sec;
    lua_pushnumber(L, ftime);
}

static struct timeval* poptimeval(lua_State *L, int i, struct timeval *tv)
{
    lua_Number ipart, fpart;
    fpart = modf(luaL_optnumber(L, i, 0), &ipart);
    tv->tv_sec = (long)ipart;
    tv->tv_usec = (long)floor(fpart * 1000000);
    return tv;
}

static int lusb_try_lock_events(lua_State *L)
{
    libusb_context *ctx;
    lua_settop(L, 1);
    if (lua_isuserdata(L, 1))
    	ctx = getctx(L, 1);
    else
	ctx = defctx(L);
    lua_pushboolean(L, libusb_try_lock_events(ctx) == 0);
    return 1;
}

static int lusb_lock_events(lua_State *L)
{
    libusb_context *ctx;
    lua_settop(L, 1);
    if (lua_isuserdata(L, 1))
    	ctx = getctx(L, 1);
    else
	ctx = defctx(L);
    libusb_lock_events(ctx);
    return 0;
}

static int lusb_unlock_events(lua_State *L)
{
    libusb_context *ctx;
    lua_settop(L, 1);
    if (lua_isuserdata(L, 1))
    	ctx = getctx(L, 1);
    else
	ctx = defctx(L);
    libusb_unlock_events(ctx);
    return 0;
}

static int lusb_event_handling_ok(lua_State *L)
{
    libusb_context *ctx;
    lua_settop(L, 1);
    if (lua_isuserdata(L, 1))
    	ctx = getctx(L, 1);
    else
	ctx = defctx(L);
    lua_pushboolean(L, libusb_event_handling_ok(ctx));
    return 1;
}

static int lusb_event_handler_active(lua_State *L)
{
    libusb_context *ctx;
    lua_settop(L, 1);
    if (lua_isuserdata(L, 1))
    	ctx = getctx(L, 1);
    else
	ctx = defctx(L);
    lua_pushboolean(L, libusb_event_handler_active(ctx));
    return 1;
}

static int lusb_lock_event_waiters(lua_State *L)
{
    libusb_context *ctx;
    lua_settop(L, 1);
    if (lua_isuserdata(L, 1))
    	ctx = getctx(L, 1);
    else
	ctx = defctx(L);
    libusb_lock_event_waiters(ctx);
    return 0;
}

static int lusb_unlock_event_waiters(lua_State *L)
{
    libusb_context *ctx;
    lua_settop(L, 1);
    if (lua_isuserdata(L, 1))
    	ctx = getctx(L, 1);
    else
	ctx = defctx(L);
    libusb_unlock_event_waiters(ctx);
    return 0;
}

static int lusb_wait_for_event(lua_State *L)
{
    libusb_context *ctx;
    struct timeval tv;
    struct timeval *ptv = NULL;
    lua_settop(L, 2);
    if (lua_isuserdata(L, 1))
    {
    	ctx = getctx(L, 1);
	if (!lua_isnil(L, 2))
	    ptv = poptimeval(L, 2, &tv);
    }
    else
    {
	ctx = defctx(L);
	if (!lua_isnil(L, 1))
	    ptv = poptimeval(L, 1, &tv);
    }
    lua_pushboolean(L, libusb_wait_for_event(ctx, ptv) == 0);
    return 1;
}

static int lusb_handle_events(lua_State *L)
{
    libusb_context *ctx;
    int err;
    lua_settop(L, 1);
    if (lua_isuserdata(L, 1))
    	ctx = getctx(L, 1);
    else
	ctx = defctx(L);
    if ((err = libusb_handle_events(ctx)) != 0)
	return _err(L, err);
    lua_pushboolean(L, 1);
    return 1;
}

static int lusb_handle_events_timeout(lua_State *L)
{
    libusb_context *ctx;
    struct timeval tv;
    int err;
    lua_settop(L, 2);
    if (lua_isuserdata(L, 1))
    {
    	ctx = getctx(L, 1);
	poptimeval(L, 2, &tv);
    }
    else
    {
	ctx = defctx(L);
	poptimeval(L, 1, &tv);
    }
    if ((err = libusb_handle_events_timeout(ctx, &tv)) != 0)
	return _err(L, err);
    lua_pushboolean(L, 1);
    return 1;
}

static int lusb_handle_events_locked(lua_State *L)
{
    libusb_context *ctx;
    struct timeval tv;
    int err;
    lua_settop(L, 2);
    if (lua_isuserdata(L, 1))
    {
    	ctx = getctx(L, 1);
	poptimeval(L, 2, &tv);
    }
    else
    {
	ctx = defctx(L);
	poptimeval(L, 1, &tv);
    }
    if ((err = libusb_handle_events_locked(ctx, &tv)) != 0)
	return _err(L, err);
    lua_pushboolean(L, 1);
    return 1;
}

static int lusb_pollfds_handle_timeouts(lua_State *L)
{
    libusb_context *ctx;
    lua_settop(L, 1);
    if (lua_isuserdata(L, 1))
    	ctx = getctx(L, 1);
    else
	ctx = defctx(L);
    lua_pushboolean(L, libusb_pollfds_handle_timeouts(ctx));
    return 1;
}

static int lusb_get_next_timeout(lua_State *L)
{
    libusb_context *ctx;
    struct timeval tv;
    int err;
    lua_settop(L, 1);
    if (lua_isuserdata(L, 1))
    	ctx = getctx(L, 1);
    else
	ctx = defctx(L);
    err = libusb_get_next_timeout(ctx, &tv);
    if (err < 0)
	return _err(L, err);
    if (err)
	pushtimeval(L, &tv);
    else
	lua_pushnumber(L, 0);
    return 1;
}


static const luaL_Reg lusb_ctx_methods[] = {
    {"set_debug", lusb_set_debug},
    {"get_device_list", lusb_get_device_list},
    {"try_lock_events", lusb_try_lock_events},
    {"lock_events", lusb_lock_events},
    {"unlock_events", lusb_unlock_events},
    {"event_handling_ok", lusb_event_handling_ok},
    {"event_handler_active", lusb_event_handler_active},
    {"lock_event_waiters", lusb_lock_event_waiters},
    {"unlock_event_waiters", lusb_unlock_event_waiters},
    {"wait_for_event", lusb_wait_for_event},
    {"handle_events", lusb_handle_events},
    {"handle_events_timeout", lusb_handle_events_timeout},
    {"handle_events_locked", lusb_handle_events_locked},
    {"pollfds_handle_timeouts", lusb_pollfds_handle_timeouts},
    {"get_next_timeout", lusb_get_next_timeout},
    {NULL, NULL}
};

static const luaL_Reg lusb_dev_methods[] = {
    {"get_bus_number", lusb_get_bus_number},
    {"get_device_address", lusb_get_device_address},
    {"get_max_packet_size", lusb_get_max_packet_size},
    {"get_max_iso_packet_size", lusb_get_max_iso_packet_size},
    {"get_device_descriptor", lusb_get_device_descriptor},
    {"get_active_config_descriptor", lusb_get_active_config_descriptor},
    {"get_config_descriptor", lusb_get_config_descriptor},
    {"get_config_descriptor_by_value", lusb_get_config_descriptor_by_value},
    {"open", lusb_open},
    {NULL, NULL}
};

static const luaL_Reg lusb_handle_methods[] = {
    {"close", closehandle},
    {"get_device", lusb_get_device},
    {"get_configuration", lusb_get_configuration},
    {"set_configuration", lusb_set_configuration},
    {"claim_interface", lusb_claim_interface},
    {"release_interface", lusb_release_interface},
    {"set_interface_alt_setting", lusb_set_interface_alt_setting},
    {"clear_halt", lusb_clear_halt},
    {"reset_device", lusb_reset_device},
    {"kernel_driver_active", lusb_kernel_driver_active},
    {"detach_kernel_driver", lusb_detach_kernel_driver},
    {"attach_kernel_driver", lusb_attach_kernel_driver},
    {"control_transfer", lusb_control_transfer},
    {"bulk_transfer", lusb_bulk_transfer},
    {"interrupt_transfer", lusb_interrupt_transfer},
    {"get_descriptor", lusb_get_descriptor},
    {"get_string_descriptor", lusb_get_string_descriptor},
    {"get_string_descriptor_ascii", lusb_get_string_descriptor_ascii},
    {"get_string_descriptor_utf8", lusb_get_string_descriptor_utf8},
    {NULL, NULL}
};

static const luaL_Reg lusb_transfer_methods[] = {
    {"submit_transfer", lusb_submit_transfer},
    {"cancel_transfer", lusb_cancel_transfer},
    {"transfer_get_data", lusb_transfer_get_data},
    {"control_transfer_get_data", lusb_control_transfer_get_data},
    {"control_transfer_get_setup", lusb_control_transfer_get_setup},
    {"fill_control_setup", lusb_fill_control_setup},
    {"fill_control_transfer", lusb_fill_control_transfer},
    {"fill_bulk_transfer", lusb_fill_bulk_transfer},
    {"fill_interrupt_transfer", lusb_fill_interrupt_transfer},
    {"fill_iso_transfer", lusb_fill_iso_transfer},
    {"set_iso_packet_lengths", lusb_set_iso_packet_lengths},
    {"get_iso_packet_buffer", lusb_get_iso_packet_buffer},
    {"set_iso_packet_buffer", lusb_set_iso_packet_buffer},
    {NULL, NULL}
};

static const luaL_Reg lusb_functions[] = {
    {"init", lusb_init},
    {"set_debug", lusb_set_debug},
    {"get_device_list", lusb_get_device_list},
    {"get_bus_number", lusb_get_bus_number},
    {"get_device_address", lusb_get_device_address},
    {"get_max_packet_size", lusb_get_max_packet_size},
    {"get_max_iso_packet_size", lusb_get_max_iso_packet_size},
    {"get_device_descriptor", lusb_get_device_descriptor},
    {"get_active_config_descriptor", lusb_get_active_config_descriptor},
    {"get_config_descriptor", lusb_get_config_descriptor},
    {"get_config_descriptor_by_value", lusb_get_config_descriptor_by_value},
    {"get_descriptor", lusb_get_descriptor},
    {"get_string_descriptor", lusb_get_string_descriptor},
    {"get_string_descriptor_ascii", lusb_get_string_descriptor_ascii},
    {"get_string_descriptor_utf8", lusb_get_string_descriptor_utf8},
    {"close", closehandle},
    {"open", lusb_open},
    {"open_device_with_vid_pid", lusb_open_device_with_vid_pid},
    {"get_device", lusb_get_device},
    {"get_configuration", lusb_get_configuration},
    {"set_configuration", lusb_set_configuration},
    {"claim_interface", lusb_claim_interface},
    {"release_interface", lusb_release_interface},
    {"set_interface_alt_setting", lusb_set_interface_alt_setting},
    {"clear_halt", lusb_clear_halt},
    {"reset_device", lusb_reset_device},
    {"kernel_driver_active", lusb_kernel_driver_active},
    {"detach_kernel_driver", lusb_detach_kernel_driver},
    {"attach_kernel_driver", lusb_attach_kernel_driver},
    {"control_transfer", lusb_control_transfer},
    {"bulk_transfer", lusb_bulk_transfer},
    {"interrupt_transfer", lusb_interrupt_transfer},
    {"transfer", lusb_transfer},
    {"submit_transfer", lusb_submit_transfer},
    {"cancel_transfer", lusb_cancel_transfer},
    {"transfer_get_data", lusb_transfer_get_data},
    {"control_transfer_get_data", lusb_control_transfer_get_data},
    {"control_transfer_get_setup", lusb_control_transfer_get_setup},
    {"fill_control_setup", lusb_fill_control_setup},
    {"fill_control_transfer", lusb_fill_control_transfer},
    {"fill_bulk_transfer", lusb_fill_bulk_transfer},
    {"fill_interrupt_transfer", lusb_fill_interrupt_transfer},
    {"fill_iso_transfer", lusb_fill_iso_transfer},
    {"set_iso_packet_lengths", lusb_set_iso_packet_lengths},
    {"get_iso_packet_buffer", lusb_get_iso_packet_buffer},
    {"set_iso_packet_buffer", lusb_set_iso_packet_buffer},
    {"lock_events", lusb_lock_events},
    {"unlock_events", lusb_unlock_events},
    {"event_handling_ok", lusb_event_handling_ok},
    {"event_handler_active", lusb_event_handler_active},
    {"lock_event_waiters", lusb_lock_event_waiters},
    {"unlock_event_waiters", lusb_unlock_event_waiters},
    {"wait_for_event", lusb_wait_for_event},
    {"handle_events", lusb_handle_events},
    {"handle_events_timeout", lusb_handle_events_timeout},
    {"handle_events_locked", lusb_handle_events_locked},
    {"pollfds_handle_timeouts", lusb_pollfds_handle_timeouts},
    {"get_next_timeout", lusb_get_next_timeout},
    {NULL, NULL}
};

typedef struct l_constant { const char *name; lua_Integer val; } l_constant;
#define constant(c)	{#c,c}
static const l_constant lusb_constants[] = {
    constant(LIBUSB_CLASS_PER_INTERFACE),
    constant(LIBUSB_CLASS_AUDIO),
    constant(LIBUSB_CLASS_COMM),
    constant(LIBUSB_CLASS_HID),
    constant(LIBUSB_CLASS_PRINTER),
    constant(LIBUSB_CLASS_PTP),
    constant(LIBUSB_CLASS_MASS_STORAGE),
    constant(LIBUSB_CLASS_HUB),
    constant(LIBUSB_CLASS_DATA),
    constant(LIBUSB_CLASS_WIRELESS),
    constant(LIBUSB_CLASS_APPLICATION),
    constant(LIBUSB_CLASS_VENDOR_SPEC),
    constant(LIBUSB_DT_DEVICE),
    constant(LIBUSB_DT_CONFIG),
    constant(LIBUSB_DT_STRING),
    constant(LIBUSB_DT_INTERFACE),
    constant(LIBUSB_DT_ENDPOINT),
    constant(LIBUSB_DT_HID),
    constant(LIBUSB_DT_REPORT),
    constant(LIBUSB_DT_PHYSICAL),
    constant(LIBUSB_DT_HUB),
    constant(LIBUSB_DT_DEVICE_SIZE),
    constant(LIBUSB_DT_CONFIG_SIZE),
    constant(LIBUSB_DT_INTERFACE_SIZE),
    constant(LIBUSB_DT_ENDPOINT_SIZE),
    constant(LIBUSB_DT_ENDPOINT_AUDIO_SIZE),
    constant(LIBUSB_DT_HUB_NONVAR_SIZE),
    constant(LIBUSB_ENDPOINT_ADDRESS_MASK),
    constant(LIBUSB_ENDPOINT_DIR_MASK),
    constant(LIBUSB_ENDPOINT_IN),
    constant(LIBUSB_ENDPOINT_OUT),
    constant(LIBUSB_TRANSFER_TYPE_MASK),
    constant(LIBUSB_TRANSFER_TYPE_CONTROL),
    constant(LIBUSB_TRANSFER_TYPE_ISOCHRONOUS),
    constant(LIBUSB_TRANSFER_TYPE_BULK),
    constant(LIBUSB_TRANSFER_TYPE_INTERRUPT),
    constant(LIBUSB_REQUEST_GET_STATUS),
    constant(LIBUSB_REQUEST_CLEAR_FEATURE),
    constant(LIBUSB_REQUEST_SET_FEATURE),
    constant(LIBUSB_REQUEST_SET_ADDRESS),
    constant(LIBUSB_REQUEST_GET_DESCRIPTOR),
    constant(LIBUSB_REQUEST_SET_DESCRIPTOR),
    constant(LIBUSB_REQUEST_GET_CONFIGURATION),
    constant(LIBUSB_REQUEST_SET_CONFIGURATION),
    constant(LIBUSB_REQUEST_GET_INTERFACE),
    constant(LIBUSB_REQUEST_SET_INTERFACE),
    constant(LIBUSB_REQUEST_SYNCH_FRAME),
    constant(LIBUSB_REQUEST_TYPE_STANDARD),
    constant(LIBUSB_REQUEST_TYPE_CLASS),
    constant(LIBUSB_REQUEST_TYPE_VENDOR),
    constant(LIBUSB_REQUEST_TYPE_RESERVED),
    constant(LIBUSB_RECIPIENT_DEVICE),
    constant(LIBUSB_RECIPIENT_INTERFACE),
    constant(LIBUSB_RECIPIENT_ENDPOINT),
    constant(LIBUSB_RECIPIENT_OTHER),
    constant(LIBUSB_ISO_SYNC_TYPE_MASK),
    constant(LIBUSB_ISO_SYNC_TYPE_NONE),
    constant(LIBUSB_ISO_SYNC_TYPE_ASYNC),
    constant(LIBUSB_ISO_SYNC_TYPE_ADAPTIVE),
    constant(LIBUSB_ISO_SYNC_TYPE_SYNC),
    constant(LIBUSB_ISO_USAGE_TYPE_MASK),
    constant(LIBUSB_ISO_USAGE_TYPE_DATA),
    constant(LIBUSB_ISO_USAGE_TYPE_FEEDBACK),
    constant(LIBUSB_ISO_USAGE_TYPE_IMPLICIT),
    constant(LIBUSB_CONTROL_SETUP_SIZE),
    constant(LIBUSB_SUCCESS),
    constant(LIBUSB_ERROR_IO),
    constant(LIBUSB_ERROR_INVALID_PARAM),
    constant(LIBUSB_ERROR_ACCESS),
    constant(LIBUSB_ERROR_NO_DEVICE),
    constant(LIBUSB_ERROR_NOT_FOUND),
    constant(LIBUSB_ERROR_BUSY),
    constant(LIBUSB_ERROR_TIMEOUT),
    constant(LIBUSB_ERROR_OVERFLOW),
    constant(LIBUSB_ERROR_PIPE),
    constant(LIBUSB_ERROR_INTERRUPTED),
    constant(LIBUSB_ERROR_NO_MEM),
    constant(LIBUSB_ERROR_NOT_SUPPORTED),
    constant(LIBUSB_ERROR_OTHER),
    constant(LIBUSB_TRANSFER_COMPLETED),
    constant(LIBUSB_TRANSFER_ERROR),
    constant(LIBUSB_TRANSFER_TIMED_OUT),
    constant(LIBUSB_TRANSFER_CANCELLED),
    constant(LIBUSB_TRANSFER_STALL),
    constant(LIBUSB_TRANSFER_NO_DEVICE),
    constant(LIBUSB_TRANSFER_OVERFLOW),
    constant(LIBUSB_TRANSFER_SHORT_NOT_OK),
    /* Lua clients should not be concerned with this
    constant(LIBUSB_TRANSFER_FREE_BUFFER),
    constant(LIBUSB_TRANSFER_FREE_TRANSFER),
    */
    {NULL, 0}
};

static void reg_table(lua_State *L, const char *name, const char *mode)
{
    if (luaL_newmetatable(L, name) && mode)
    {
	lua_createtable(L, 0, 1);
	lua_pushstring(L, mode);
	lua_setfield(L, -2, "__mode");
	lua_setmetatable(L, -2);
    }
    lua_pop(L, 1);
}

int luaopen_libusb1(lua_State *L)
{
    int i;
    reg_table(L, DEVICES_REG, "kv");
    reg_table(L, DEVPTR_REG, "v");
    reg_table(L, HANDLES_REG, "k");
    reg_table(L, BUFFER_REG, "k");
    reg_table(L, TRANSFER_REG, NULL);
    reg_table(L, POLLIN_REG, "k");
    reg_table(L, POLLOUT_REG, "k");
    if (luaL_newmetatable(L, CONTEXT_MT))
    {
	luaL_newlib(L, lusb_ctx_methods);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, exitctx);
	lua_setfield(L, -2, "__gc");
    }
    if (luaL_newmetatable(L, DEVICE_MT))
    {
	luaL_newlib(L, lusb_dev_methods);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, unrefdev);
	lua_setfield(L, -2, "__gc");
    }
    if (luaL_newmetatable(L, HANDLE_MT))
    {
	luaL_newlib(L, lusb_handle_methods);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, closehandle);
	lua_setfield(L, -2, "__gc");
    }
    if (luaL_newmetatable(L, TRANSFER_MT))
    {
	luaL_newlib(L, lusb_transfer_methods);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, freetransfer);
	lua_setfield(L, -2, "__gc");
    }
    lua_pop(L, 3);
    lua_createtable(L, 0, (sizeof(lusb_functions)/sizeof(luaL_Reg))+
	    		   (sizeof(lusb_constants)/sizeof(l_constant))-2);
    luaL_setfuncs(L, lusb_functions, 0);
    for (i = 0; lusb_constants[i].name; ++i)
    {
	lua_pushinteger(L, lusb_constants[i].val);
	lua_setfield(L, -2, lusb_constants[i].name);
    }
    return 1;
}

