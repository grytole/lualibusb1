local function narrow(uni)
    local str={}
    for b=1,#uni,2 do
	str[#str+1] = string.sub(uni,b,b)
    end
    return table.concat(str)
end
local function hex(d) return ("%.4X"):format(d) end
usb = require "libusb1"
usb.set_debug(3)
devices = assert(usb.get_device_list())
for i=1,#devices do
    local dev = devices[i]
    local d = assert(usb.get_device_descriptor(dev))
    print(dev:get_bus_number()..'.'..dev:get_device_address(),
    	  d.bDeviceClass..'.'..d.bDeviceSubClass..'.'..d.bDeviceProtocol,
	  hex(d.idVendor)..':'..hex(d.idProduct),hex(d.bcdUSB),hex(d.bcdDevice),
	  d.bNumConfigurations)
    if d.bDeviceClass == 239 then
	local h = assert(usb.open(dev))
	local langs = h:get_string_descriptor(0)
	local l = langs[1] or 0
	print("",narrow(h:get_string_descriptor(d.iManufacturer,0)))
	h:close()
    end
end

