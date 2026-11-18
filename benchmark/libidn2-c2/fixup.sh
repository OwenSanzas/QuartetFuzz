# QF pipeline fix: the case image's tree is the scrubbed single commit at tag
# v2.3.8, so git-version-gen yields "2.3.8-modified" once the harness file is
# replaced, IDN2_VERSION_PATCH becomes `8-modified` and tests/test-version.c
# fails to compile (any harness).  Pin the release version instead.
echo 2.3.8 > /src/libidn2/.tarball-version
