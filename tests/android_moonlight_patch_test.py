import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SPEC=importlib.util.spec_from_file_location("patch_android_moonlight",ROOT/"scripts/patch-android-moonlight.py")
MODULE=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(MODULE)

class AndroidMoonlightPatchTest(unittest.TestCase):
    def test_patch_adds_catalog_model_and_launch_interception(self):
        source='''import com.limelight.grid.AppGridAdapter;
    private boolean showHiddenApps;
    private HashSet<Integer> hiddenAppIds = new HashSet<>();
                    AppObject existingApp = (AppObject) appGridAdapter.getItem(i);

                    // Check if this app is in the latest list
                if (updated) {
                    appGridAdapter.notifyDataSetChanged();
                }
            }
        });
    }

    @Override
    public int getAdapterFragmentLayoutId()
                AppObject app = (AppObject) appGridAdapter.getItem(pos);

                // Only open the context menu if something is running, otherwise start it
    public static class AppObject {
        public final NvApp app;
        public boolean isRunning;
        public boolean isHidden;

        public AppObject(NvApp app) {
            if (app == null) {
                throw new IllegalArgumentException("app must not be null");
            }
            this.app = app;
        }'''
        with tempfile.TemporaryDirectory() as folder:
            target=Path(folder)/"app/src/main/java/com/limelight/AppView.java";target.parent.mkdir(parents=True);target.write_text(source)
            MODULE.patch(folder);MODULE.patch(folder)
            text=target.read_text()
            self.assertIn("GamezBridge.games",text)
            self.assertIn("launchGamez(app)",text)
            self.assertIn("transportApp",text)

if __name__=="__main__":unittest.main()
