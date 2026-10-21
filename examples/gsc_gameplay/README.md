# Generic GSC gameplay example

Build from the staged project root: `xmake build replay_plugin_gsc_gameplay`. Copy the resulting DLL and
manifest from `xmake-out/examples/gsc_gameplay` into a plugin
directory. Requires MW 1.44 and the GSC-call, rich-value and typed-dvar capabilities.

Run `gsc_gameplay_status` after the map is ready to read `getdvar`, iterate the
snapshot returned by `getentarray`, and resolve `cl_textChatEnabled` through the
generated dvar mapping. Run `gsc_gameplay_give` to give each connected player's
entity an M4 using the stock `giveweapon` method. Normal stock gameplay rules,
weapon availability and script argument validation still apply.

Both commands schedule work at the verified idle server boundary. A GSC native
callback must also schedule generic calls; calling directly during active VM
execution returns `IW8X_BUSY`. Collection results own copies of their data and
retain generation-checked entity identities, without retaining engine objects.
