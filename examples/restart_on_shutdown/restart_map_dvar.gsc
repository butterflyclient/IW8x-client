main()
{
    level thread monitorrestartmap();
}

monitorrestartmap()
{
    level endon("game_ended");
    setdvarifuninitialized("map_restart", 0);

    for (;;)
    {
        wait 1;
        if (getdvarint("map_restart", 0) > 0)
        {
            setdvar("map_restart", 0);
            restartmap();
        }
    }
}

restartmap()
{
    // This function runs in the level thread, so target each player explicitly.
    foreach (player in level.players)
    {
        player iprintlnbold("^1Restarting Map...");
        player freezecontrols(1);
        player setclientomnvar("ui_total_fade", 1);
    }

    wait 0.5;
    level notify("restarting");
    map_restart(1);
}
