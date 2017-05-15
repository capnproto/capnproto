// Copyright (c) 2016 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#if KJ_HAS_OPENSSL

#include "tls.h"
#include <kj/test.h>
#include <kj/async-io.h>
#include <stdlib.h>

namespace kj {
namespace {

// =======================================================================================
// test data
//
// made with make-test-certs.sh

static constexpr char CA_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n";

static constexpr char INTERMEDIATE_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIGDjCCA/agAwIBAgICEAAwDQYJKoZIhvcNAQELBQAwgacxCzAJBgNVBAYTAlVT\n"
    "MRMwEQYDVQQIDApDYWxpZm9ybmlhMRIwEAYDVQQHDAlQYWxvIEFsdG8xFTATBgNV\n"
    "BAoMDFNhbmRzdG9ybS5pbzEbMBkGA1UECwwSVGVzdGluZyBEZXBhcnRtZW50MRcw\n"
    "FQYDVQQDDA5jYS5leGFtcGxlLmNvbTEiMCAGCSqGSIb3DQEJARYTZ2FycGx5QHNh\n"
    "bmRzdG9ybS5pbzAgFw0xNjA1MzEwNDI4NDZaGA8yMTE2MDUwNzA0Mjg0NlowgZcx\n"
    "CzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRUwEwYDVQQKDAxTYW5k\n"
    "c3Rvcm0uaW8xGzAZBgNVBAsMElRlc3RpbmcgRGVwYXJ0bWVudDEbMBkGA1UEAwwS\n"
    "aW50LWNhLmV4YW1wbGUuY29tMSIwIAYJKoZIhvcNAQkBFhNnYXJwbHlAc2FuZHN0\n"
    "b3JtLmlvMIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAtqWYNn5PO7cG\n"
    "gyFzFX3q4QUQuKX+mzHjf560nOiF0Hon4SnZHnJqfGTSPBiEtqsaYLLf4dmO2SkR\n"
    "w65MRKy8A6/441NhmCv8OamepOJWB71jlGVuLIoUwYgvT0R529r+Kzdq2xcnSgOo\n"
    "O54SB4d8sSALAfOMKky+EOqcQq7VC+JZtel8LEJUpfvicHZtwS64D4IetsaGuAPz\n"
    "KCZ3QTWMZr+FPzvmTJnxmrVwHU/whT8ma8rFljZ3oQiawIzeb/hQ4OjGdjt/H3TZ\n"
    "A5rWL5s3t6RbR/MhLkajH4Fpw1qQUt5aHm54czLnHBQ9yz4dP/oWxZ8GBWT9PV63\n"
    "CW34Irb6Zb5bmfYpaDtd0XsGACYCmLwDxBH2G5UZ6Z5nazjOHAQcYb6f4kjYVuoG\n"
    "PvD5pjsZ7pcZr//gb4+SsVgMQDC1jRNoJPS9G3Wz42msR9IAqFaSUuay6viTPQqf\n"
    "mQGOMPzJFbxnje0U483YotL+DhqtV5zFxx1l1nNJL7/5AdSPBRCvriLzIxblemBq\n"
    "5JdlkKcqWq/QJtvrzHnlixVCKA1mHaWUB7zv7Np0KzeHWZ6sIcBlR7No0boW4Xmm\n"
    "S4F9sTIfh+4f6qhXnjq3G4yIXhggHIWOutFAgupMQv5oNZYDjqzBhDIfWXK/8V9P\n"
    "/3ubm34tOsNcWwpxxiEc5dkQAvLmFycCAwEAAaNQME4wHQYDVR0OBBYEFMvpWUv7\n"
    "zweivFl4A6ZykYOe9hogMB8GA1UdIwQYMBaAFNIcrdRyL5MclnpDhYZuBnZ2X7Lx\n"
    "MAwGA1UdEwQFMAMBAf8wDQYJKoZIhvcNAQELBQADggIBAFyOD1Zti+4awHLMYDwn\n"
    "s6YMSKDNnG1GjpNVjkOPZ2BOAbg18fcsNmy69R4S4rfRQMIEZPNSXh092v4BKUmc\n"
    "Afa4ahEZmDET4plRWWpWUC8j8/ESIv2372QG2Q00TNWmhPiUKd8VMwV4HFcHml/+\n"
    "99BrTNn68s8zMB5BNiJgWTxFIRPLy12FWjr0OriVp1EaPOstkYNxfl4RdBgTXCJ8\n"
    "A9XRBNc25JHPNGPVqiRmCpewsSzEmP2sdz1MGspX7xiaE0UWnCyMPOmX90+dZlSI\n"
    "GHoBI6iNL894pzKiGvl4dZ2cwKhMncXqvbx9v+IDGSXrC6bsZNl42wee8D7axsKE\n"
    "xGNQKcGpU8EQqt6YC2wFV+wAHIlYuH90yk1LAZrehKy0E7N/ToKBz7MgGtoQLbn6\n"
    "UuHZto/LthCY6A8ppgqrq1JEbuLoqskRuOCIF6qXjPTsNhsBXCU/xnkae74QO+2r\n"
    "adrOubDvIjNtaHBuDgAeVl6PIP4mcJUMF3KhdVg/EDXWVq34YSEF+G/hR/Cj8GKl\n"
    "DIGGtcQ2re+uPAoPnQ+II9iJcDeY+1wpo9HK4VL54ERzXd53Vl357mNuA/69Fjh3\n"
    "HRzI5ELdyzWOEFCteCWyPtlOCZJ3Uuj9umS9K/B7KM1XmRzTOYvvj7aTaT4DhoO6\n"
    "VfvEtl+RmGSm/YmfTJ8551Zq\n"
    "-----END CERTIFICATE-----\n";

static constexpr char HOST_KEY[] =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "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\n"
    "-----END RSA PRIVATE KEY-----\n";

static constexpr char VALID_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIF9zCCA9+gAwIBAgICEAAwDQYJKoZIhvcNAQELBQAwgZcxCzAJBgNVBAYTAlVT\n"
    "MRMwEQYDVQQIDApDYWxpZm9ybmlhMRUwEwYDVQQKDAxTYW5kc3Rvcm0uaW8xGzAZ\n"
    "BgNVBAsMElRlc3RpbmcgRGVwYXJ0bWVudDEbMBkGA1UEAwwSaW50LWNhLmV4YW1w\n"
    "bGUuY29tMSIwIAYJKoZIhvcNAQkBFhNnYXJwbHlAc2FuZHN0b3JtLmlvMCAXDTE2\n"
    "MDUzMTA0Mjg0N1oYDzIxMTYwNTMxMDQyODQ3WjCBkDELMAkGA1UEBhMCVVMxEzAR\n"
    "BgNVBAgMCkNhbGlmb3JuaWExFTATBgNVBAoMDFNhbmRzdG9ybS5pbzEbMBkGA1UE\n"
    "CwwSVGVzdGluZyBEZXBhcnRtZW50MRQwEgYDVQQDDAtleGFtcGxlLmNvbTEiMCAG\n"
    "CSqGSIb3DQEJARYTZ2FycGx5QHNhbmRzdG9ybS5pbzCCAiIwDQYJKoZIhvcNAQEB\n"
    "BQADggIPADCCAgoCggIBANsHStqtDiv+hz3eunDuTrcY/207sgCEbdObcpANvLVz\n"
    "jSc67B9aemMqa/yT8BmHkgTock1DSkEOOmEnsvszkzt1R9tf66XXhAJglYMv3I7S\n"
    "WqMmAFbkn+vuTXB6Yf6D1/cEalQG9wY6Y0Aci6+BLynYStkvq9ACXDdW3j1iUiKU\n"
    "LptBIJsc9c9jlkEV9Pt8lfLWULZNYiSr5jyNSTRSKkLIEDMUIAUEsrEYHxasMioA\n"
    "4BB6FGKhL1Ns2XjkNaJ80TS8X95rHmc/SRrGk2qmI7OL7DkimEE+VHGvre6tO2dm\n"
    "xX/e5CkyXVVeDbV8sm4Vl8b5tafjEqEkqU4OO6DeAFRh++T7rJo52oI1pTu60wGY\n"
    "de0IgDkVxoBpWEalQmCjzHWoXoRI+3LqTZ+eZsR+GGqJha8Iz2sl2v9U7F8z9e2+\n"
    "Wk0jDcntKujcDjCFBjgMp/MExFEkgwxKwuJ4oUR8Rm95BU6KtXagiPUnPS9R2Oe0\n"
    "qfLaSuQkA9Rt6ac4cGdv15/OIqUK4qILFeQC28bpAu1hmY04AWobDGQah1rViLjl\n"
    "IwCq/1/46aDYAaZE+lS6qcfS+049c47xcC7BNGAEKt+5cdkmpv/gkswNwRw7Mehj\n"
    "UqbUSimLz0iGAxcSVL3/yh4PJiP4J6mtBIByb0zwTku4KNRuDoT/SFII1qKFzITz\n"
    "AgMBAAGjUDBOMB0GA1UdDgQWBBT2qSSBGCUEYfohpsiHlq9yECjAqDAfBgNVHSME\n"
    "GDAWgBTL6VlL+88HorxZeAOmcpGDnvYaIDAMBgNVHRMEBTADAQH/MA0GCSqGSIb3\n"
    "DQEBCwUAA4ICAQCb34A5Hz6iI80U+mSnkvOnVtaqyxnsVcBbfMcsRyGG/GSVBNJD\n"
    "zcCxnxrPc0NXcsK3wIR7KU1oQmusNCtI2XNd1lceBytQD6TDzcuuNCpjF7Uh+pdi\n"
    "AL2HzDoy9q4Mxxk1wTGDuyy+4opZQG12fe9pr4wo93/BXbE4kDrSzp/2iTQp9/zh\n"
    "JRrRISwyFH6HKX/MoVbpJfAqiMHXHeHylH6h4lUVVfYFSSB8PWL3lxAwCM0ECGmd\n"
    "ZMGyh089ViW0mBoF5lacwHkAnw17S0JrUM9+66oPRLIo2rsgnNj4sMo/9dzSJ9/T\n"
    "OneewTK4O8t/mZNp1auYFC1+m8wWRh0G5Y5CwZ1CJqy8mHMd/33FbMO+MyyEeFkG\n"
    "DHNzCYEupp1ymqvFZK8TyIX1m/QOy0W6NT6INFY1dy3CoJWnMAJRKvxeFQGJ28Up\n"
    "wYPZPj7xxGb6TdgVC0c7kMCorIu3tsLLRtwtAbN/D8ogS78QwgyLqJ41friFMD+9\n"
    "AS1sjfqiiC4hpr11z+xdCJLb4vkBsigHuofjx3uyiueyKXQTFQCXjF2w1FbJTKSZ\n"
    "kQ4CP2eG8UN5BN46kih89NjBUCduXq/x4SJCEjtG57QFBogmcG/OTX1pCOwjnK4L\n"
    "z6Kz4+2VTNYHvIXSIoicO+jZeISYpllg6ISNBeqoOp2zp6Rf6rwR6/YZog==\n"
    "-----END CERTIFICATE-----\n";

static constexpr char EXPIRED_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIF9TCCA92gAwIBAgICEAAwDQYJKoZIhvcNAQELBQAwgZcxCzAJBgNVBAYTAlVT\n"
    "MRMwEQYDVQQIDApDYWxpZm9ybmlhMRUwEwYDVQQKDAxTYW5kc3Rvcm0uaW8xGzAZ\n"
    "BgNVBAsMElRlc3RpbmcgRGVwYXJ0bWVudDEbMBkGA1UEAwwSaW50LWNhLmV4YW1w\n"
    "bGUuY29tMSIwIAYJKoZIhvcNAQkBFhNnYXJwbHlAc2FuZHN0b3JtLmlvMB4XDTE2\n"
    "MDEwMTAwMDAwMFoXDTE2MDEwMTAwMDAwMFowgZAxCzAJBgNVBAYTAlVTMRMwEQYD\n"
    "VQQIDApDYWxpZm9ybmlhMRUwEwYDVQQKDAxTYW5kc3Rvcm0uaW8xGzAZBgNVBAsM\n"
    "ElRlc3RpbmcgRGVwYXJ0bWVudDEUMBIGA1UEAwwLZXhhbXBsZS5jb20xIjAgBgkq\n"
    "hkiG9w0BCQEWE2dhcnBseUBzYW5kc3Rvcm0uaW8wggIiMA0GCSqGSIb3DQEBAQUA\n"
    "A4ICDwAwggIKAoICAQDbB0rarQ4r/oc93rpw7k63GP9tO7IAhG3Tm3KQDby1c40n\n"
    "OuwfWnpjKmv8k/AZh5IE6HJNQ0pBDjphJ7L7M5M7dUfbX+ul14QCYJWDL9yO0lqj\n"
    "JgBW5J/r7k1wemH+g9f3BGpUBvcGOmNAHIuvgS8p2ErZL6vQAlw3Vt49YlIilC6b\n"
    "QSCbHPXPY5ZBFfT7fJXy1lC2TWIkq+Y8jUk0UipCyBAzFCAFBLKxGB8WrDIqAOAQ\n"
    "ehRioS9TbNl45DWifNE0vF/eax5nP0kaxpNqpiOzi+w5IphBPlRxr63urTtnZsV/\n"
    "3uQpMl1VXg21fLJuFZfG+bWn4xKhJKlODjug3gBUYfvk+6yaOdqCNaU7utMBmHXt\n"
    "CIA5FcaAaVhGpUJgo8x1qF6ESPty6k2fnmbEfhhqiYWvCM9rJdr/VOxfM/XtvlpN\n"
    "Iw3J7Sro3A4whQY4DKfzBMRRJIMMSsLieKFEfEZveQVOirV2oIj1Jz0vUdjntKny\n"
    "2krkJAPUbemnOHBnb9efziKlCuKiCxXkAtvG6QLtYZmNOAFqGwxkGoda1Yi45SMA\n"
    "qv9f+Omg2AGmRPpUuqnH0vtOPXOO8XAuwTRgBCrfuXHZJqb/4JLMDcEcOzHoY1Km\n"
    "1Eopi89IhgMXElS9/8oeDyYj+CeprQSAcm9M8E5LuCjUbg6E/0hSCNaihcyE8wID\n"
    "AQABo1AwTjAdBgNVHQ4EFgQU9qkkgRglBGH6IabIh5avchAowKgwHwYDVR0jBBgw\n"
    "FoAUy+lZS/vPB6K8WXgDpnKRg572GiAwDAYDVR0TBAUwAwEB/zANBgkqhkiG9w0B\n"
    "AQsFAAOCAgEAtIjwUBNVrc62XK36bHHFLawYJaJUNj4zwdBUmVc5BL0tPHfDIwhP\n"
    "GBbxZCrAbPlHUkWig6l52S2lG0Aq2rtBwZPrjSJMio4ln/3y9qZjdqCjKtiQy7lc\n"
    "jQu9wXzlcTomARt2NcgTMedOrC4+eXMExUZ+n7xHKsj9u6sYeH0LXtMfg0Xzloc7\n"
    "ojF+ISM3bZbZQzGNkG+Fz1OCGgJ6W/wHxAadIHMmOMl1YN9ZRbO7T93AlMSGpehi\n"
    "LYh/n8B769yyxlfMWIM/aGECZdLyE5NUeN2r0jfjSkgp9l9dXr5fJL7bA54YcQC4\n"
    "dWjuMf8tpPpI1580fU5xD0ta6NGeLkQ5lEUexIaH0vSmUDs0N9HpTooGFcONl75R\n"
    "sEaB7/xH/ZGjtRXTc/QAPyfzFvbQNgRSoiif7DHRt7Wv13fKt/xL9uZlTGZUfojt\n"
    "eIFe82dNIffpoXkkHGFlpxxgnpwMb62N7BFqi2uJrSasrNEYZuEKTQMb1p76qojp\n"
    "6zAUPaut+FHgM5zVaKkvdXvJEReHG7un2a/DfZelIa8VIWO4BGQJBhSaKbRiGXiu\n"
    "2BbV3/qp9R0msirfyesBa/NV1syw2PYoouYukSdMfROK4r2FGPN7cw0AYrY3NbGG\n"
    "5jFKu2Vr1krHysnmFyXhkoyVSy4dYjrrqa1ItZW9SF0f83IN54C/P7E=\n"
    "-----END CERTIFICATE-----\n";

static constexpr char SELF_SIGNED_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIGHzCCBAegAwIBAgIJAN7Q46+wlXJHMA0GCSqGSIb3DQEBCwUAMIGkMQswCQYD\n"
    "VQQGEwJVUzETMBEGA1UECAwKQ2FsaWZvcm5pYTESMBAGA1UEBwwJUGFsbyBBbHRv\n"
    "MRUwEwYDVQQKDAxTYW5kc3Rvcm0uaW8xGzAZBgNVBAsMElRlc3RpbmcgRGVwYXJ0\n"
    "bWVudDEUMBIGA1UEAwwLZXhhbXBsZS5jb20xIjAgBgkqhkiG9w0BCQEWE2dhcnBs\n"
    "eUBzYW5kc3Rvcm0uaW8wIBcNMTYwNTMxMDQyODQ3WhgPMjExNjA1MzEwNDI4NDda\n"
    "MIGkMQswCQYDVQQGEwJVUzETMBEGA1UECAwKQ2FsaWZvcm5pYTESMBAGA1UEBwwJ\n"
    "UGFsbyBBbHRvMRUwEwYDVQQKDAxTYW5kc3Rvcm0uaW8xGzAZBgNVBAsMElRlc3Rp\n"
    "bmcgRGVwYXJ0bWVudDEUMBIGA1UEAwwLZXhhbXBsZS5jb20xIjAgBgkqhkiG9w0B\n"
    "CQEWE2dhcnBseUBzYW5kc3Rvcm0uaW8wggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAw\n"
    "ggIKAoICAQDbB0rarQ4r/oc93rpw7k63GP9tO7IAhG3Tm3KQDby1c40nOuwfWnpj\n"
    "Kmv8k/AZh5IE6HJNQ0pBDjphJ7L7M5M7dUfbX+ul14QCYJWDL9yO0lqjJgBW5J/r\n"
    "7k1wemH+g9f3BGpUBvcGOmNAHIuvgS8p2ErZL6vQAlw3Vt49YlIilC6bQSCbHPXP\n"
    "Y5ZBFfT7fJXy1lC2TWIkq+Y8jUk0UipCyBAzFCAFBLKxGB8WrDIqAOAQehRioS9T\n"
    "bNl45DWifNE0vF/eax5nP0kaxpNqpiOzi+w5IphBPlRxr63urTtnZsV/3uQpMl1V\n"
    "Xg21fLJuFZfG+bWn4xKhJKlODjug3gBUYfvk+6yaOdqCNaU7utMBmHXtCIA5FcaA\n"
    "aVhGpUJgo8x1qF6ESPty6k2fnmbEfhhqiYWvCM9rJdr/VOxfM/XtvlpNIw3J7Sro\n"
    "3A4whQY4DKfzBMRRJIMMSsLieKFEfEZveQVOirV2oIj1Jz0vUdjntKny2krkJAPU\n"
    "bemnOHBnb9efziKlCuKiCxXkAtvG6QLtYZmNOAFqGwxkGoda1Yi45SMAqv9f+Omg\n"
    "2AGmRPpUuqnH0vtOPXOO8XAuwTRgBCrfuXHZJqb/4JLMDcEcOzHoY1Km1Eopi89I\n"
    "hgMXElS9/8oeDyYj+CeprQSAcm9M8E5LuCjUbg6E/0hSCNaihcyE8wIDAQABo1Aw\n"
    "TjAdBgNVHQ4EFgQU9qkkgRglBGH6IabIh5avchAowKgwHwYDVR0jBBgwFoAU9qkk\n"
    "gRglBGH6IabIh5avchAowKgwDAYDVR0TBAUwAwEB/zANBgkqhkiG9w0BAQsFAAOC\n"
    "AgEA0dYsDTtt75fqQ35+ZHZGFCzJ4eUM4m/IdXfMlVw5Eb+2XA7pAVQgHhUD2qU0\n"
    "6Xk5EqINr7X8TvvR3OGVzlAw7BD4ZVKXkNy82BwRoydSUN9GM+Q1yqHES8+dBH5D\n"
    "jdrBq5XdtG3cQEjxxHPb5iWc5MLvVM0UtuuBtk3rp9IoY3+ReczGbm8CXep7jyIv\n"
    "W0wTi8gjMPWqn/f4ikkprMh0hXCwRTojfWi+Gl1sKyzviG+FF3hJj/fNf7IKYG8d\n"
    "lO/Ay9wqY+j7oMIz6RUUOht31tLnTaSSDDknuHF+r0GJoNWjsncf/NseGDLBiTkZ\n"
    "bBGMJhB3Pd6FNcLuREtE1WT5EwJr9WZeSh1mQPssQvwgYjdCI558sw1WEBdON73M\n"
    "0/aZzc/gDx9G1zxUxzn85/pxpBeQgb+J8iaAz1Iy9Gn64A6rVbQWAb5/xZ4y1LSe\n"
    "xTFVcK9spSf3k2FX24DPOov3oLu7vGmgye76bD4I0WtcVFFK5vXsfXGHq+P8EXv5\n"
    "F2KmlitfAh6N+uSeWzROrwU7roYsg81epvXVTYR/MyXNEB46iRMNdOqWThpdteEk\n"
    "qts6SreMmr0+aX7oqJ52Ohtw437JvxMqd5PNHuU3qQQS2o7cJzlZ+dDQMoZdxNaV\n"
    "bWPnPRk8plkBJLuQZA7KcTGA3b6qHl0hMTDJUE8bscNmMs4=\n"
    "-----END CERTIFICATE-----\n";

// =======================================================================================

class ErrorNexus {
  // Helper class that wraps various promises such that if one throws an exception, they all do.

public:
  ErrorNexus(): ErrorNexus(kj::newPromiseAndFulfiller<void>()) {}

  template <typename T>
  kj::Promise<T> wrap(kj::Promise<T>&& promise) {
    return promise.catch_([this](kj::Exception&& e) -> kj::Promise<T> {
      fulfiller->reject(kj::cp(e));
      return kj::mv(e);
    }).exclusiveJoin(failurePromise.addBranch().then([]() -> T { KJ_UNREACHABLE; }));
  }

private:
  kj::ForkedPromise<void> failurePromise;
  kj::Own<kj::PromiseFulfiller<void>> fulfiller;

  ErrorNexus(kj::PromiseFulfillerPair<void> paf)
      : failurePromise(kj::mv(paf.promise).fork()),
        fulfiller(kj::mv(paf.fulfiller)) {}
};

struct TlsTest {
  kj::AsyncIoContext io = setupAsyncIo();
  TlsContext tlsClient;
  TlsContext tlsServer;

  TlsTest(TlsContext::Options clientOpts = defaultClient(),
          TlsContext::Options serverOpts = defaultServer())
      : tlsClient(kj::mv(clientOpts)),
        tlsServer(kj::mv(serverOpts)) {}

  static TlsContext::Options defaultServer() {
    static TlsKeypair keypair = {
      TlsPrivateKey(HOST_KEY),
      TlsCertificate(kj::str(VALID_CERT, INTERMEDIATE_CERT))
    };
    TlsContext::Options options;
    options.defaultKeypair = keypair;
    return options;
  }

  static TlsContext::Options defaultClient() {
    static TlsCertificate caCert(CA_CERT);
    TlsContext::Options options;
    options.useSystemTrustStore = false;
    options.trustedCertificates = kj::arrayPtr(&caCert, 1);
    return options;
  }
};

KJ_TEST("TLS basics") {
  TlsTest test;
  ErrorNexus e;

  auto pipe = test.io.provider->newTwoWayPipe();

  auto clientPromise = e.wrap(test.tlsClient.wrapClient(kj::mv(pipe.ends[0]), "example.com"));
  auto serverPromise = e.wrap(test.tlsServer.wrapServer(kj::mv(pipe.ends[1])));

  auto client = clientPromise.wait(test.io.waitScope);
  auto server = serverPromise.wait(test.io.waitScope);

  auto writePromise = client->write("foo", 3);
  char buf[4];
  server->read(&buf, 3).wait(test.io.waitScope);
  buf[3] = '\0';

  KJ_ASSERT(kj::StringPtr(buf) == "foo");
}

class TestSniCallback: public TlsSniCallback {
public:
  kj::Maybe<TlsKeypair> getKey(kj::StringPtr hostname) override {
    ++callCount;

    KJ_ASSERT(hostname == "example.com");
    return TlsKeypair {
      TlsPrivateKey(HOST_KEY),
      TlsCertificate(kj::str(VALID_CERT, INTERMEDIATE_CERT))
    };
  }

  uint callCount = 0;
};

KJ_TEST("TLS SNI") {
  TlsContext::Options serverOptions;
  TestSniCallback callback;
  serverOptions.sniCallback = callback;

  TlsTest test(TlsTest::defaultClient(), serverOptions);
  ErrorNexus e;

  auto pipe = test.io.provider->newTwoWayPipe();

  auto clientPromise = e.wrap(test.tlsClient.wrapClient(kj::mv(pipe.ends[0]), "example.com"));
  auto serverPromise = e.wrap(test.tlsServer.wrapServer(kj::mv(pipe.ends[1])));

  auto client = clientPromise.wait(test.io.waitScope);
  auto server = serverPromise.wait(test.io.waitScope);

  auto writePromise = client->write("foo", 3);
  char buf[4];
  server->read(&buf, 3).wait(test.io.waitScope);
  buf[3] = '\0';

  KJ_ASSERT(kj::StringPtr(buf) == "foo");

  KJ_ASSERT(callback.callCount == 1);
}

void expectInvalidCert(kj::StringPtr hostname, TlsCertificate cert, kj::StringPtr message) {
  TlsKeypair keypair = { TlsPrivateKey(HOST_KEY), kj::mv(cert) };
  TlsContext::Options serverOpts;
  serverOpts.defaultKeypair = keypair;
  TlsTest test(TlsTest::defaultClient(), kj::mv(serverOpts));
  ErrorNexus e;

  auto pipe = test.io.provider->newTwoWayPipe();

  auto clientPromise = e.wrap(test.tlsClient.wrapClient(kj::mv(pipe.ends[0]), hostname));
  auto serverPromise = e.wrap(test.tlsServer.wrapServer(kj::mv(pipe.ends[1])));

  KJ_EXPECT_THROW_MESSAGE(message, clientPromise.wait(test.io.waitScope));
}

KJ_TEST("TLS certificate validation") {
  expectInvalidCert("wrong.com", TlsCertificate(kj::str(VALID_CERT, INTERMEDIATE_CERT)),
                    "Hostname mismatch");
  expectInvalidCert("example.com", TlsCertificate(VALID_CERT),
                    "unable to get local issuer certificate");
  expectInvalidCert("example.com", TlsCertificate(kj::str(EXPIRED_CERT, INTERMEDIATE_CERT)),
                    "certificate has expired");
  expectInvalidCert("example.com", TlsCertificate(SELF_SIGNED_CERT),
                    "self signed certificate");
}

#ifdef KJ_EXTERNAL_TESTS
KJ_TEST("TLS to capnproto.org") {
  kj::AsyncIoContext io = setupAsyncIo();
  TlsContext tls;

  auto network = tls.wrapNetwork(io.provider->getNetwork());
  auto addr = network->parseAddress("capnproto.org", 443).wait(io.waitScope);
  auto stream = addr->connect().wait(io.waitScope);

  kj::StringPtr request =
      "HEAD / HTTP/1.1\r\n"
      "Host: capnproto.org\r\n"
      "Connection: close\r\n"
      "User-Agent: capnp-test/0.6\r\n"
      "\r\n";

  stream->write(request.begin(), request.size()).wait(io.waitScope);

  char buffer[4096];
  size_t n = stream->tryRead(buffer, sizeof(buffer) - 1, sizeof(buffer) - 1).wait(io.waitScope);
  buffer[n] = '\0';
  kj::StringPtr response(buffer, n);

  KJ_ASSERT(response.startsWith("HTTP/1.1 200 OK\r\n"));
}
#endif

}  // namespace
}  // namespace kj

#endif  // KJ_HAS_OPENSSL
