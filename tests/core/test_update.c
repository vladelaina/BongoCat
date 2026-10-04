#include "test.h"
#include "bongo_cat/update.h"

#include <string.h>

static const char release_json[] =
    "{"
    "\"tag_name\":\"v1.2.3\","
    "\"html_url\":\"https://github.com/vladelaina/BongoCat/releases/tag/v1.2.3\","
    "\"draft\":false,\"prerelease\":false,"
    "\"body\":\"Fixed input and rendering.\","
    "\"assets\":["
    "{\"name\":\"BongoCat-1.2.3-windows-x64-portable.exe.sha256\","
    "\"browser_download_url\":\"https://github.com/vladelaina/BongoCat/"
    "releases/download/v1.2.3/BongoCat-1.2.3-windows-x64-portable.exe.sha256\"},"
    "{\"name\":\"BongoCat-1.2.3-windows-x64-setup.exe\","
    "\"browser_download_url\":\"https://github.com/vladelaina/BongoCat/"
    "releases/download/v1.2.3/BongoCat-1.2.3-windows-x64-setup.exe\"},"
    "{\"name\":\"BongoCat-1.2.3-windows-x64-portable.exe\","
    "\"browser_download_url\":\"https://github.com/vladelaina/BongoCat/"
    "releases/download/v1.2.3/BongoCat-1.2.3-windows-x64-portable.exe\"}]}";

static const char unix_release_json[] =
    "{\"tag_name\":\"v2.0.0\","
    "\"html_url\":\"https://github.com/vladelaina/BongoCat/releases/tag/v2.0.0\","
    "\"draft\":false,\"prerelease\":false,\"assets\":["
    "{\"name\":\"BongoCat-2.0.0-linux-x64.tar.gz\","
    "\"browser_download_url\":\"https://github.com/vladelaina/BongoCat/"
    "releases/download/v2.0.0/BongoCat-2.0.0-linux-x64.tar.gz\"}]}";

void test_update(void) {
    CHECK(bongo_cat_update_version_valid("1.2.3-rc.1+build.4"));
    CHECK(!bongo_cat_update_version_valid("1"));
    CHECK(!bongo_cat_update_version_valid("1."));
    CHECK(!bongo_cat_update_version_valid("1.2"));
    CHECK(bongo_cat_update_compare_versions("1.2.3", "1.2.2") > 0);
    CHECK(bongo_cat_update_compare_versions("v2.0.0", "1.99.99") > 0);
    CHECK(bongo_cat_update_compare_versions("1.0.0", "1.0.0-rc.1") > 0);
    CHECK(bongo_cat_update_compare_versions("1.0.0-beta.2",
        "1.0.0-beta.11") < 0);
    CHECK(bongo_cat_update_compare_versions("1.0.0+build.2",
        "1.0.0+build.1") == 0);

    BongoCatUpdateRelease release;
    BongoCatError error = {0};
    CHECK(bongo_cat_update_parse_release(release_json, "windows-x64",
        &release, &error));
    CHECK(strcmp(release.version, "1.2.3") == 0);
    CHECK(strstr(release.installer_url, "windows-x64-setup.exe") != NULL);
    CHECK(strstr(release.portable_url, "windows-x64-portable.exe") != NULL);
    CHECK(strcmp(release.notes, "Fixed input and rendering.") == 0);

    CHECK(bongo_cat_update_parse_release(unix_release_json, "linux-x64",
        &release, &error));
    CHECK(release.installer_url[0] == '\0');
    CHECK(strstr(release.portable_url, "linux-x64.tar.gz") != NULL);

    const char unsafe[] =
        "{\"tag_name\":\"v2.0.0\",\"draft\":false,"
        "\"prerelease\":false,\"html_url\":\"https://example.com/v2\","
        "\"assets\":[]}";
    CHECK(!bongo_cat_update_parse_release(unsafe, "windows-x64",
        &release, &error));

    const char oversized_tag[] =
        "{\"tag_name\":\"v1234567890123456789012345678901234567890\","
        "\"draft\":false,\"prerelease\":false,"
        "\"html_url\":\"https://github.com/vladelaina/BongoCat/"
        "releases/tag/oversized\",\"assets\":[]}";
    CHECK(!bongo_cat_update_parse_release(oversized_tag, "windows-x64",
        &release, &error));
}
