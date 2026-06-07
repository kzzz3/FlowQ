import json
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
CMAKE_PRESETS = REPO_ROOT / "CMakePresets.json"
VCPKG_MANIFEST = REPO_ROOT / "vcpkg.json"


def cache_value(cache, key):
    value = cache.get(key)
    if isinstance(value, dict):
        return value.get("value")
    return value


def manifest_features(value):
    if value is None:
        return set()
    if isinstance(value, list):
        return {str(item) for item in value}
    return {item for item in str(value).replace(",", ";").split(";") if item}


def dependency_names(dependencies):
    names = []
    for dependency in dependencies:
        if isinstance(dependency, str):
            names.append(dependency)
        elif isinstance(dependency, dict) and "name" in dependency:
            names.append(dependency["name"])
    return names


class CMakeInteropConfigurationTests(unittest.TestCase):
    def test_vcpkg_interop_feature_supplies_ngtcp2(self):
        manifest = json.loads(VCPKG_MANIFEST.read_text(encoding="utf-8"))

        self.assertIn("interop", manifest.get("features", {}))
        interop_feature = manifest["features"]["interop"]
        self.assertIn("ngtcp2", dependency_names(interop_feature.get("dependencies", [])))

    def test_vcpkg_interop_presets_enable_manifest_interop_feature(self):
        presets = json.loads(CMAKE_PRESETS.read_text(encoding="utf-8"))
        checked_presets = []

        for preset in presets.get("configurePresets", []):
            cache = preset.get("cacheVariables", {})
            if cache_value(cache, "FLOWQ_BUILD_INTEROP") != "ON":
                continue
            toolchain = str(cache_value(cache, "CMAKE_TOOLCHAIN_FILE") or "")
            if "vcpkg.cmake" not in toolchain:
                continue

            checked_presets.append(preset.get("name", "<unnamed>"))
            features = manifest_features(cache_value(cache, "VCPKG_MANIFEST_FEATURES"))
            self.assertIn(
                "interop",
                features,
                f"{preset.get('name', '<unnamed>')} must enable the vcpkg interop feature "
                "so external peer dependencies are installed with the interop build.",
            )

        self.assertTrue(checked_presets, "expected at least one vcpkg-backed interop configure preset")


if __name__ == "__main__":
    unittest.main()
