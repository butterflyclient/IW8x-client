# Discord Map Webhook Example

This native plugin posts the current `ui_mapname` to a Discord webhook.

The plugin never accesses a game RVA directly. It subscribes to the public
`IW8X_SCRIPTS_LOADED` lifecycle event, schedules a safe game-thread poll, reads
`ui_mapname` through the read-only plugin dvar API, then sends the webhook on
the host worker path.

## Configure the webhook

The example source currently contains a webhook URL directly. For a real
plugin, prefer loading the webhook URL from configuration or an environment
variable instead of committing it to source control.

Do not commit a real Discord webhook URL to source control. Anyone with the URL can post through that webhook.

## Build

From the staged project root:

```powershell
xmake build replay_plugin_discord_webhook
```

The target outputs:

```text
xmake-out/x64/Release/plugin-examples/discord_webhook/
├── discord_webhook.dll
└── plugin.json
```

## Runtime behavior

No replacement `.gscbin` is required.

When scripts finish loading, the plugin polls `ui_mapname` on the server frame
until it becomes non-empty, then sends it once for that script epoch.

The resulting Discord message looks like:

```text
ui_mapname: `mp_don4_pm`
```

The HTTP request runs on the plugin worker path rather than blocking the GSC/server thread.

For a direct transport test from the server console, run:

```text
discord_webhook_test mp_shipment
```
