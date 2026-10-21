// Compile with tools/compile_plugin_gsc.py and the server's native-bindings.json.
// The wrapper expands friendly names before invoking the stock IW8 compiler.
#inline iw8x_native_bindings;

// Invoke from an existing map/player script; custom ScriptFile discovery is
// still owned by the existing IW8x GSCBIN loader.
native_example()
{
    result = native_add(10, 20);
    self native_test();
    self waittill("native_test_complete", message, number);
    return result;
}
