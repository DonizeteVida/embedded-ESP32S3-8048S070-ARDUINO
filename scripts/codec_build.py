"""Keep PNGdec's bundled zlib separate from unzipLIB's modified inflater."""
Import("env")

def png_symbols(build_env, node):
    isolated = build_env.Clone()
    isolated.Append(CPPDEFINES=["Z_PREFIX", ("z_errmsg", "png_z_errmsg")])
    return isolated.Object(node)

env.AddBuildMiddleware(png_symbols, "*/PNGdec/src/*")

def native_unzip(build_env, node):
    isolated = build_env.Clone()
    # Old bundled zlib mistakes modern Darwin for classic Mac OS. Prevent its
    # fdopen replacement from corrupting the system stdio declaration.
    if build_env.subst("$PIOPLATFORM") == "native":
        isolated.Append(CPPDEFINES=[("fdopen", "fdopen")])
    return isolated.Object(node)

env.AddBuildMiddleware(native_unzip, "*/unzipLIB/src/*")
