// SPDX-License-Identifier: GPL-2.0-only
#include <dtv/host_ts/px4_ts_core.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

using px4::userland::cli::Px4TsArguments;
using px4::userland::cli::parse_px4_ts_product_arguments;

Px4TsArguments parse(std::vector<std::string> values)
{
    std::vector<const char*> argv;
    for (const auto& value : values) argv.push_back(value.c_str());
    return parse_px4_ts_product_arguments(static_cast<int>(argv.size()), argv.data());
}

bool check(bool condition, const char* message)
{
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

}  // namespace

int main()
{
    bool ok = true;
    const auto version = parse({"px4-ts", "--version"});
    ok &= check(version.valid && version.version && version.product_cli && !version.help,
                "--version is standalone");
    ok &= check(!parse({"px4-ts", "--version", "--receiver", "0"}).valid,
                "--version cannot be combined");

    const auto t13 = parse({"px4-ts", "--device", "serial:00001205000960",
                            "--receiver", "0", "-c", "T13"});
    ok &= check(t13.valid && !t13.duration_set && t13.frequency_hz == 473142857U,
                "runtime-dir is optional and T13 resolves to canonical Hz");
    const auto t27 = parse({"px4-ts", "--device", "serial:00001205000960",
                            "--receiver", "0", "-f", "557142857", "-s", "isdb-t",
                            "--runtime-dir", "/tmp/px4-ts-runtime"});
    ok &= check(t27.valid && t27.frequency_hz == 557142857U,
                "explicit runtime-dir and Hz input are retained");

    for (const auto* spelling : {"isdb-t", "ISDB-T", "IsDb-T"}) {
        const auto matched = parse({"px4-ts", "--device", "serial:00001205000960",
                                    "--receiver", "0", "--freq", "557142857",
                                    "--system", spelling});
        ok &= check(matched.valid, "terrestrial system names are case-insensitive");
    }
    ok &= check(!parse({"px4-ts", "--device", "serial:00001205000960",
                         "--receiver", "0", "--freq", "557142857",
                         "--system", "ISDB-T2"}).valid,
                "extended system names are rejected");
    ok &= check(!parse({"px4-ts", "--device", "serial:00001205000960",
                         "--receiver", "0", "--frequency-khz", "557142"}).valid,
                "legacy frequency alias is rejected");
    return ok ? 0 : 1;
}
