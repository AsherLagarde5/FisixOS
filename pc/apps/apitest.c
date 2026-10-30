/* apitest.fse - exercises the .fse API v2 directly, for QEMU testing.
 * Prints one line per check, ending with APITEST_DONE.
 * Writes are attempted on purpose: in Liveboot they must fail
 * (by design), in Installed mode they must succeed.
 */
#include "../boot/fse.h"

static void out(fisix_api_t *api, const char *s) { api->print(s); }
static void nl(fisix_api_t *api) { api->print_newline(); }

int fse_main(fisix_api_t *api)
{
    char buf[1024];
    int n;
    unsigned int y, mo, d, h, mi, se;
    out(api, "APITEST_START"); nl(api);
    n = api->dir_list("", buf, sizeof(buf));
    out(api, "dir_root_count="); api->print_u64((unsigned long long)(n < 0 ? 0 : n)); nl(api);
    if (n > 0) { out(api, buf); }
    n = api->dir_list("SYSTEM\\APPS", buf, sizeof(buf));
    out(api, "dir_apps_count="); api->print_u64((unsigned long long)(n < 0 ? 0 : n)); nl(api);
    if (n > 0) { out(api, buf); }
    out(api, "exists_kernel="); api->print_u64((unsigned long long)api->file_exists("kernel.bin")); nl(api);
    out(api, "exists_nope="); api->print_u64((unsigned long long)api->file_exists("nope123.txt")); nl(api);
    out(api, "size_kernel="); api->print_u64((unsigned long long)(api->file_size("kernel.bin") < 0 ? 0 : api->file_size("kernel.bin"))); nl(api);
    api->get_time(&y, &mo, &d, &h, &mi, &se);
    out(api, "date="); api->print_u64(y); out(api, "-"); api->print_u64(mo); out(api, "-"); api->print_u64(d); nl(api);
    out(api, "time="); api->print_u64(h); out(api, ":"); api->print_u64(mi); out(api, ":"); api->print_u64(se); nl(api);
    out(api, "mkdir="); api->print_u64((unsigned long long)(api->file_mkdir("APITESTDIR") == 0 ? 1 : 0)); nl(api);
    out(api, "write="); api->print_u64((unsigned long long)(api->file_write("APITESTDIR\\apitest.txt", "hello api", 9) == 0 ? 1 : 0)); nl(api);
    out(api, "exists_apitest="); api->print_u64((unsigned long long)api->file_exists("APITESTDIR\\apitest.txt")); nl(api);
    n = api->file_read("APITESTDIR\\apitest.txt", buf, sizeof(buf));
    out(api, "read_apitest="); if (n >= 0) out(api, buf); nl(api);
    out(api, "rename="); api->print_u64((unsigned long long)(api->file_rename("APITESTDIR\\apitest.txt", "APITESTDIR\\renamed.txt") == 0 ? 1 : 0)); nl(api);
    out(api, "exists_renamed="); api->print_u64((unsigned long long)api->file_exists("APITESTDIR\\renamed.txt")); nl(api);
    out(api, "delete="); api->print_u64((unsigned long long)(api->file_delete("APITESTDIR\\renamed.txt") == 0 ? 1 : 0)); nl(api);
    out(api, "exists_after_del="); api->print_u64((unsigned long long)api->file_exists("APITESTDIR\\renamed.txt")); nl(api);
    out(api, "rename_dir="); api->print_u64((unsigned long long)(api->file_rename("APITESTDIR", "APITESTDIR2") == 0 ? 1 : 0)); nl(api);
    out(api, "APITEST_DONE"); nl(api);
    return 0;
}
