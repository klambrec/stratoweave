const std = @import("std");

// Acton links mbedTLS already, but does not re-export its public headers.
// Export headers from that exact runtime version, avoiding a second crypto
// implementation or an ABI mismatch with separately fetched mbedTLS.
pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const deps = b.option([]const u8, "acton_sysdeps", "Acton runtime dependency directory") orelse @panic("acton_sysdeps required");
    const lib = b.addLibrary(.{
        .name = "oidc_crypto_headers",
        .linkage = .static,
        .root_module = b.createModule(.{ .target = target, .optimize = optimize }),
    });
    lib.root_module.addCSourceFile(.{ .file = b.path("empty.c"), .flags = &.{} });
    lib.installHeadersDirectory(.{ .cwd_relative = b.pathJoin(&.{ deps, "mbedtls/include/mbedtls" }) }, "mbedtls", .{});
    lib.installHeadersDirectory(.{ .cwd_relative = b.pathJoin(&.{ deps, "mbedtls/include/psa" }) }, "psa", .{});
    lib.installHeader(.{ .cwd_relative = b.pathJoin(&.{ deps, "libyyjson/yyjson.h" }) }, "yyjson.h");
    b.installArtifact(lib);
}
