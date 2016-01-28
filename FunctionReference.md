# lualibusb1 Function Reference #
Functions from libusb are exported in the module namespace. Functions that take a context, device, handle, or transfer as the first argument can be called as methods on the appropriate object. The functions are named the same as in the library, but with the `libusb_` prefix removed.

Constants from libusb are exported in the module namespace with the same name as in the library including the `LIBUSB_` prefix.

If a function fails , it will return `nil`, an error string, and the error number. A Lua error will be raised because of incorrect arguments or if the default context cannot be created.

For more detailed information and usage instructions, please read the [library documentation](http://libusb.sourceforge.net/api-1.0/).

## Context Functions ##
A default context is created automatically when needed. If you omit the context argument from a function the default will be used.

> `init() -- ctx`

> `set_debug([ctx,] level)`

> `get_device_list([ctx]) -- table`

> `open_device_with_vid_pid([ctx, ] vendorid, productid) -- handle`

## Descriptor Functions ##

> `get_device_descriptor(dev) -- table`

> `get_active_config_descriptor(dev) -- table`

> `get_config_descriptor(dev, index) -- table`

> `get_config_descriptor_by_value(dev, value) -- table`

> `get_descriptor(handle, type, index) -- table or string`

If the descriptor type is know, a table will be returned. Otherwise the entire structure is returned as a string.

> `get_string_descriptor(handle, index [, langid]) -- string or table`

The index 0 is reserved for reporting available languages. The function will then return a table with each language ID as a number. Strings are returned in UTF-16 encoding. (Or is it UCS-2?)

> `get_string_descriptor_ascii(handle, index) -- string`

> `get_string_descriptor_utf8(handle, index) -- string`

## Devices ##

> `open(dev) -- handle`

> `get_bus_number(dev) -- number`

> `get_device_address(dev) -- number`

> `get_max_packet_size(dev, endpoint) -- number`

> `get_max_iso_packet_size(dev, endpoint) -- number`

## Device Handles ##

> `get_device(handle) -- dev`

> `get_configuration(handle) -- number`

> `set_configuration(handle, cfg) -- handle`

> `claim_interface(handle, iface) -- handle`

> `release_interface(handle, iface) -- handle`

> `set_interface_alt_setting(handle, iface, alternate) -- handle`

> `clear_halt(handle, endpoint) -- handle`

> `reset_device(handle) -- handle`

> `kernel_driver_active(handle, iface) -- boolean`

> `detach_kernel_driver(handle, iface) -- handle`

> `attach_kernel_driver(handle, iface) -- handle`

## Synchronous I/O ##
A transfer from the host to the device requires a string and returns the number of bytes actually sent. A transfer from the device to the host requires the number of bytes to read and returns a string. Bulk and interrupt transfers also return a boolean that is `true` if the transfer did not complete before the timout.

> `control_transfer(handle, type, request, value, index, data-or-length [, timeout]) -- string or number`

> `bulk_transfer(handle, endpoint, data-or-length [, timeout]) -- string or number, boolean`

> `interrupt_transfer(handle, endpoint, data-or-length [, timeout]) -- string or number, boolean`

## Asynchronous I/O ##
Unlike the libusb library, the callback and timeout values are given when you submit a transfer not when you fill it. This is to make garbage collection easier.

> `transfer([num_iso_packets]) -- transfer`

> `submit_transfer(transfer [, callback [, timeout]])`

When the transfer completes, the callback will be called with the transfer object, the status, and the actual number of bytes transferred.

> `cancel_transfer(transfer)`

> `transfer_get_data(transfer) -- string`

> `control_transfer_get_data(transfer) -- string`

> `control_transfer_get_setup(transfer) -- table`

> `fill_control_setup(transfer, setup-table [, data-or-length]) -- transfer`

The setup options are set by the table. The table can also include the data at index 1, or the length with the key `wLength`. This function can be called multiple times on the same transfer. Settings that are not passed to the function will remain unchanged.

> `fill_control_transfer(transfer, handle [, setup] [, data-or-length]) -- transfer`

The setup options and data can passed in this function which is the same as calling `fill_control_setup` first.

> `fill_bulk_transfer(transfer, handle, endpoint, data-or-length) -- transfer`

> `fill_interrupt_transfer(transfer, handle, endpoint, data-or-length) -- transfer`

> `fill_iso_transfer(transfer, handle, endpoint, data-or-length, num-packets) -- transfer`

> `set_iso_packet_lengths(transfer, table-or-number) -- transfer`

Most transfers will use the same size for all packets. Setting the lengths to a number will change all packets in the transfer. Otherwise, pass a table as a list of lengths for each packet.

> `get_iso_packet_buffer(transfer, number) -- string, status`

Returns the actual length of the packet that was transfered. The results may not be meaningful except when called from a callback function. The first packet is number 0.

> `set_iso_packet_buffer(transfer, number, string) -- transfer`

The first packet is number 0.

## Events and Polling ##

Timeout duration is in seconds and may have a fractional part. The exact resolution depends on the operating system.

> `try_lock_events([ctx]) -- boolean`

Returns `true` if the lock was obtained successfully.

> `lock_events([ctx])`

> `unlock_events([ctx])`

> `event_handling_ok([ctx]) -- boolean`

> `event_handler_active([ctx]) -- boolean`

> `lock_event_waiters([ctx])`

> `unlock_event_waiters([ctx])`

> `wait_for_event([ctx, ] [timeout]) -- boolean`

Returns `true` after a transfer completes or another thread stops event handling. A `nil` timeout will wait forever.

> `handle_events([ctx]) -- boolean`

> `handle_events_timeout([ctx, ] [timeout]) -- boolean`

The default timeout is 0.

> `handle_events_locked([ctx, ] [timeout]) -- boolean`

The default timeout is 0.

> `pollfds_handle_timeouts([ctx]) -- boolean`

Returns `true` if all timeout events are handled internally or `false` if your application must call into libusb at times determined by `libusb_get_next_timeout()`.

> `get_next_timeout([ctx]) -- number`

> `get_pollfds([ctx]) -- in-list, out-list`

Returns two tables. The first is a list of reading file descriptors. The second is a list of writing file descriptors. You should wait on the files using a library such as [lua-ev](https://github.com/brimworks/lua-ev).

> `set_pollfd_notifiers([ctx, ] pollfd_add_cb, pollfd_rem_cb)`

Set functions that will be called when a file descriptor is added or removed.