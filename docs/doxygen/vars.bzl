def _doxygen_vars_impl(ctx):
    vars = dict(ctx.attr.variables)

    # Provide $(KEY) expansions for files (quoted; supports spaces in paths).
    for key, tgt in ctx.attr.locations.items():
        files = tgt[DefaultInfo].files.to_list()
        vars[key] = '"%s"' % '" "'.join([f.path for f in files])

    return [platform_common.TemplateVariableInfo(vars)]

doxygen_vars = rule(
    implementation = _doxygen_vars_impl,
    attrs = {
        "variables": attr.string_dict(),
        "locations": attr.string_keyed_label_dict(allow_files = True),
    },
)
