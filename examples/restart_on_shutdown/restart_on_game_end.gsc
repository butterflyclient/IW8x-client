// Compile with tools/compile_plugin_gsc.py using the server's
// native-bindings.json so iw8x_restart_map resolves to this plugin's
// registered native function.
#inline iw8x_native_bindings;

main()
{
    level thread restart_on_game_end();
}

restart_on_game_end()
{
    // endon("game_ended") would terminate this thread. To execute the restart
    // when the notify fires, this watcher must wait for that notify instead.
    level waittill("game_ended");
    // Native queues the player message/freeze/fade, waits 0.5 seconds, then
    // sends level notify("restarting") before map_restart(1).
    iw8x_restart_map();
}
