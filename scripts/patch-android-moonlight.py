#!/usr/bin/env python3
"""Add FlXtR's shared Gamez catalog overlay to pinned Moonlight Android."""
from pathlib import Path
import sys


def replace_once(text, before, after, label):
    if after in text:
        return text
    if text.count(before) != 1:
        raise RuntimeError(f"Moonlight {label} changed; review the Android integration")
    return text.replace(before, after)


def patch(tree):
    path = Path(tree) / "app/src/main/java/com/limelight/AppView.java"
    text = path.read_text()
    text = replace_once(text, "import com.limelight.grid.AppGridAdapter;", "import com.limelight.grid.AppGridAdapter;\nimport com.limelight.flxtr.GamezBridge;", "imports")
    text = replace_once(text, "    private boolean showHiddenApps;\n    private HashSet<Integer> hiddenAppIds = new HashSet<>();", "    private boolean showHiddenApps;\n    private boolean gamezLoading;\n    private boolean gamezLoaded;\n    private HashSet<Integer> hiddenAppIds = new HashSet<>();", "state")
    text = replace_once(text,
        "                if (updated) {\n                    appGridAdapter.notifyDataSetChanged();\n                }\n            }\n        });\n    }\n\n    @Override\n    public int getAdapterFragmentLayoutId()",
        '''                if (!gamezLoaded && !gamezLoading && GamezBridge.configured(AppView.this)) {
                    for (NvApp app : appList) {
                        if (app.getAppName().equalsIgnoreCase("FlXtR Gamez")) {
                            loadGamezCatalog(app);
                            break;
                        }
                    }
                }

                if (updated) {
                    appGridAdapter.notifyDataSetChanged();
                }
            }
        });
    }

    private void loadGamezCatalog(final NvApp transport) {
        gamezLoading = true;
        final String host = computer.activeAddress == null ? null : computer.activeAddress.address;
        new Thread(() -> {
            try {
                final List<GamezBridge.Game> games = GamezBridge.games(AppView.this, host);
                runOnUiThread(() -> {
                    for (int i = appGridAdapter.getCount() - 1; i >= 0; i--) {
                        AppObject item = (AppObject) appGridAdapter.getItem(i);
                        if (item.app.getAppId() == transport.getAppId()) appGridAdapter.removeApp(item);
                    }
                    for (GamezBridge.Game game : games) {
                        int syntheticId = Integer.MIN_VALUE | (game.id.hashCode() & 0x3fffffff);
                        appGridAdapter.addApp(new AppObject(new NvApp(game.title, syntheticId, false), transport, game.id));
                    }
                    gamezLoading = false;
                    gamezLoaded = true;
                    appGridAdapter.notifyDataSetChanged();
                    if (games.isEmpty()) Toast.makeText(AppView.this, "No approved PC games", Toast.LENGTH_SHORT).show();
                });
            } catch (Exception error) {
                runOnUiThread(() -> {
                    gamezLoading = false;
                    gamezLoaded = true;
                    Toast.makeText(AppView.this, error.getMessage(), Toast.LENGTH_LONG).show();
                });
            }
        }, "FlXtR-Gamez-Catalog").start();
    }

    private void launchGamez(final AppObject app) {
        Toast.makeText(this, "Starting " + app.app.getAppName() + "...", Toast.LENGTH_SHORT).show();
        final String host = computer.activeAddress == null ? null : computer.activeAddress.address;
        new Thread(() -> {
            try {
                GamezBridge.launch(AppView.this, host, app.gamezId);
                runOnUiThread(() -> ServerHelper.doStart(AppView.this, app.transportApp, computer, managerBinder));
            } catch (Exception error) {
                runOnUiThread(() -> Toast.makeText(AppView.this, error.getMessage(), Toast.LENGTH_LONG).show());
            }
        }, "FlXtR-Gamez-Launch").start();
    }

    @Override
    public int getAdapterFragmentLayoutId()''', "catalog overlay")
    text = replace_once(text,
        "                AppObject app = (AppObject) appGridAdapter.getItem(pos);\n\n                // Only open the context menu if something is running, otherwise start it",
        "                AppObject app = (AppObject) appGridAdapter.getItem(pos);\n                if (app.gamezId != null) {\n                    launchGamez(app);\n                    return;\n                }\n\n                // Only open the context menu if something is running, otherwise start it", "launch interception")
    text = replace_once(text,
        "                    AppObject existingApp = (AppObject) appGridAdapter.getItem(i);\n\n                    // Check if this app is in the latest list",
        "                    AppObject existingApp = (AppObject) appGridAdapter.getItem(i);\n                    if (existingApp.gamezId != null) {\n                        i++;\n                        continue;\n                    }\n\n                    // Check if this app is in the latest list", "catalog retention")
    text = replace_once(text,
        '''    public static class AppObject {
        public final NvApp app;
        public boolean isRunning;
        public boolean isHidden;

        public AppObject(NvApp app) {
            if (app == null) {
                throw new IllegalArgumentException("app must not be null");
            }
            this.app = app;
        }''',
        '''    public static class AppObject {
        public final NvApp app;
        public final NvApp transportApp;
        public final String gamezId;
        public boolean isRunning;
        public boolean isHidden;

        public AppObject(NvApp app) {
            this(app, null, null);
        }

        public AppObject(NvApp app, NvApp transportApp, String gamezId) {
            if (app == null) {
                throw new IllegalArgumentException("app must not be null");
            }
            this.app = app;
            this.transportApp = transportApp;
            this.gamezId = gamezId;
        }''', "app model")
    path.write_text(text)


if __name__ == "__main__":
    patch(sys.argv[1])
