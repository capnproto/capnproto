"Provides dependency version mappings for Cap'n'Proto and downstream Bazel projects."


## Brotli version to depend on.
BROTLI = struct(
    version = "1.1.0",
    digest = "e720a6ca29428b803f4ad165371771f5398faba397edf6778837a18599ea13ff",
)

## Zlib version to depend on.
ZLIB = struct(
    version = "1.3",
    digest = "8a9ba2898e1d0d774eca6ba5b4627a11e5588ba85c8851336eb38de4683050a7",
)

## Skylib version to depend on.
SKYLIB = struct(
    version = "1.4.2",
    digest = "66ffd9315665bfaafc96b52278f57c7e2dd09f5ede279ea6d39b2be471e7e3aa",
)

## BoringSSL version (or commit) to depend on. This should be from the `master-with-bazel` branch.
BORINGSSL_COMMIT = "ed2e74e737dc802ed9baad1af62c1514430a70d6"

BORINGSSL = struct(
    version = BORINGSSL_COMMIT,
    prefix = "google-boringssl-%s" % BORINGSSL_COMMIT[0:7],
    digest = "873ec711658f65192e9c58554ce058d1cfa4e57e13ab5366ee16f76d1c757efc",
)
