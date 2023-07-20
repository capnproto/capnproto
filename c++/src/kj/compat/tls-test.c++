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

#if _WIN32
#include <kj/win32-api-version.h>
#endif

#include "tls.h"

#include "http.h"

#include <openssl/opensslv.h>

#include <stdlib.h>

#if _WIN32
#include <winsock2.h>

#include <kj/windows-sanity.h>
#else
#include <sys/socket.h>
#endif

#include <kj/async-io.h>
#include <kj/test.h>

namespace kj {
namespace {

// =======================================================================================
// test data
//
// made with make-test-certs.sh
static constexpr char CA_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIGMzCCBBugAwIBAgIUDxGXACZeJ0byrswV8gyWskZF2Q8wDQYJKoZIhvcNAQEL\n"
    "BQAwgacxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRIwEAYDVQQH\n"
    "DAlQYWxvIEFsdG8xFTATBgNVBAoMDFNhbmRzdG9ybS5pbzEbMBkGA1UECwwSVGVz\n"
    "dGluZyBEZXBhcnRtZW50MRcwFQYDVQQDDA5jYS5leGFtcGxlLmNvbTEiMCAGCSqG\n"
    "SIb3DQEJARYTZ2FycGx5QHNhbmRzdG9ybS5pbzAgFw0yMDA2MjcwMDQyNTJaGA8y\n"
    "MTIwMDYwMzAwNDI1MlowgacxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9y\n"
    "bmlhMRIwEAYDVQQHDAlQYWxvIEFsdG8xFTATBgNVBAoMDFNhbmRzdG9ybS5pbzEb\n"
    "MBkGA1UECwwSVGVzdGluZyBEZXBhcnRtZW50MRcwFQYDVQQDDA5jYS5leGFtcGxl\n"
    "LmNvbTEiMCAGCSqGSIb3DQEJARYTZ2FycGx5QHNhbmRzdG9ybS5pbzCCAiIwDQYJ\n"
    "KoZIhvcNAQEBBQADggIPADCCAgoCggIBAKpp8VF/WDPw1V1aD36/uDWI4XRk9OaJ\n"
    "i8tkAbTPutJ7NU4AWv9OzreKIR1PPhj0DtxVOj5KYRTwL1r4DsFWh6D0rBV7oz7o\n"
    "zP8hWznVQBSa2BJ2E4uDD8p5oNz1+O+o4UgSBbOr83Gp5SZGw9KO7cgNql9Id/Ii\n"
    "sHYxXdrYdAl6InuR6q52CJgcGqQgpFYG+KYqDiByfX52slyz5FA/dfZxsmoEVFLB\n"
    "rgbeuhsJGIoasTkGIdCYJhYI7k2uWtvYNurnhgvlpfPHHuSnJ+aWVdKaQUthgbsy\n"
    "T2XHuLYpWx7+B7kCF5B4eKtn3e7yzE7A8jn8Teq6yRNUh1PnM7CRMmz3g4UAxmJT\n"
    "F5NyQd14IqveFuOk40Ba8wLoypll5We5tV7StUyvaOlAhi+gGPHfWKk4MGiBoaOV\n"
    "52D1+/dkh/abokdKZtE59gJX0MrH6mihfc9KQs7N51IhSs4kG5zGIBdvgXtmP17H\n"
    "hixUFi0Y85pqGidW4LLQ1pmK9k0U4gYlwHtqHh8an35/vp/mFhl2BDHcpuYKZ34U\n"
    "ZDo9GglfCTVEsUvAnwfhuYN0e0kveSTuRCMltjGg0Fs1h9ljNNuc46W4qIx/d5ls\n"
    "aMOTKc3PTtwtqgXOXRFn2U7AUXOgtEqyqpuj5ZcjH2YQ3BL24qAiYEHaBOHM+8qF\n"
    "9JLZE64j5dnZAgMBAAGjUzBRMB0GA1UdDgQWBBTmqsbDUpi5hgPbcPESYR9t8jsD\n"
    "7jAfBgNVHSMEGDAWgBTmqsbDUpi5hgPbcPESYR9t8jsD7jAPBgNVHRMBAf8EBTAD\n"
    "AQH/MA0GCSqGSIb3DQEBCwUAA4ICAQADdVBYClYWqNk1s2gamjGsyQ2r88TWTD6X\n"
    "RySVnyQPATWuEctrr6+8qTrbqBP4bTPKE+uTzwk+o5SirdJJAkrcwEsSCFw7J/qf\n"
    "5U/mXN+EUuqyiMHOS/vLe5X1enj0I6fqJY2mCGFD7Jr/+el1XXjzRZsLZHmqSxww\n"
    "T+UjJP+ffvtq3pq4nMQowxXm+Wub0gFHj5wkKMTIDyqnbjzB9bdVd0crtA+EpYIi\n"
    "f8y5WB46g1CngRnMzRQvg5FCmxg57i+mVgiUjUe54VenwK9aeeHIuOdLCZ0RmiNH\n"
    "KHPUBct+S/AXx8DCoAdm51EahwMBnlUyISpwJ+LVMWA2R9DOxdhEF0tv5iBsD9rn\n"
    "oKIWoa0t/Vwnd2n8wyLhuA7N4yzm0rdBjO/rU6n0atIab5+CEDyLeyWQBVwfCUF5\n"
    "XYNxOBJgGfSgJa23KUtn15pS/nSTa6sOtS/Mryc4UuNzxn+3ebNOG4UPlH6miSMK\n"
    "yA+5SCyKgrn3idifzrq+XafA2WUnxdBLgJMM4OIPAGNjCCW2P1cP/NVllUTjTy2y\n"
    "AIKQ/D9V/DzlbIIT6F3CNnqa9xnrBWTKF1YH/zSB7Gh2xlr0WnOWJVQbNUYet982\n"
    "JL5ibRhsiqBgltgQPhKhN/rGuh7Cb28679fQqLXKgOWvV2fC4b2y0v9dG78jGCEE\n"
    "LBzBUUmunw==\n"
    "-----END CERTIFICATE-----\n";

static constexpr char INTERMEDIATE_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIGETCCA/mgAwIBAgICEAAwDQYJKoZIhvcNAQELBQAwgacxCzAJBgNVBAYTAlVT\n"
    "MRMwEQYDVQQIDApDYWxpZm9ybmlhMRIwEAYDVQQHDAlQYWxvIEFsdG8xFTATBgNV\n"
    "BAoMDFNhbmRzdG9ybS5pbzEbMBkGA1UECwwSVGVzdGluZyBEZXBhcnRtZW50MRcw\n"
    "FQYDVQQDDA5jYS5leGFtcGxlLmNvbTEiMCAGCSqGSIb3DQEJARYTZ2FycGx5QHNh\n"
    "bmRzdG9ybS5pbzAgFw0yMDA2MjcwMDQyNTNaGA8yMTIwMDYwMzAwNDI1M1owgZcx\n"
    "CzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRUwEwYDVQQKDAxTYW5k\n"
    "c3Rvcm0uaW8xGzAZBgNVBAsMElRlc3RpbmcgRGVwYXJ0bWVudDEbMBkGA1UEAwwS\n"
    "aW50LWNhLmV4YW1wbGUuY29tMSIwIAYJKoZIhvcNAQkBFhNnYXJwbHlAc2FuZHN0\n"
    "b3JtLmlvMIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAvDRmV4e7F/1K\n"
    "TaySC2xq00aYZfKJmwcvBvLM60xMrwDf5KlHWoYzog72q6qNb0TpvaeOlymU1IfR\n"
    "DkRIMFz6qd4QpK3Ah0vDtqEsoYE6F7gr2QAAaB7HQhczClh6FFkMCygrHDRQlJcF\n"
    "VseTKxnZBUAUhnG7OI8bHMZsprg31SNG1GCXq/CO/rkKIP1Pdwevr8DFHL3LFF03\n"
    "vdeo6+f/3LjlsCzVCNsCcAYIScRSl1scj2QYcwP7tTmDRAmU9EZFv9MWSqRIMT4L\n"
    "/4tP9AL/pWGdx8RRvbXoLJQf+hQW6YnSorRmIH/xYsvqMaan4P0hkomfIHP4nMYa\n"
    "LgI8VsNhTeDZ7IvSF2F73baluTOHhUD5eE/WDffNeslaCoMbH/B3H6ks0zYt/mHG\n"
    "mDaw3OxgMYep7TIE+SABOSJV+pbtQWNyM7u2+TYHm2DaxD3quf+BoYUZT01uDtN4\n"
    "BSsR7XEzF25w/4lDxqBxGAZ0DzItK0kzqMykSWvDIjpSg/UjRj05sc+5zcgE4pX1\n"
    "nOLD+FuB9jVqo6zCiIkHsSI0XHnm4D6awB1UyDwSh8mivfUDT53OpOIwOI3EB/4U\n"
    "iZstUKgyXNrXsE6wS/3JfdDZ9xkw4dWV+0FWKJ4W6Y8UgKvQJRChpCUtcuxfaLjX\n"
    "/ZIcMRYEFNjFppka/7frNT1VNnRvJ9cCAwEAAaNTMFEwHQYDVR0OBBYEFNnHDzWZ\n"
    "NC5pP6njUnf1BMXuQKnnMB8GA1UdIwQYMBaAFOaqxsNSmLmGA9tw8RJhH23yOwPu\n"
    "MA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQELBQADggIBAG0ShYBIbwGMDUBj\n"
    "9G2vxI4Nx7lip9/wO5NCC2biVxtcjNxwTuCcqCxeSGgdimo82vJTm9Wa1AkoawSv\n"
    "+spoXJAeOL5dGmZX3IRD0KuDoabH7F6xbNOG3HphvwAJcKQOitPAgTt2eGXCaBqw\n"
    "a1ivxPzchHVd3+LyjLMK5G3zzgW0tZhKp7IYE0eGoSGwrw10ox6ibyswe8evul1N\n"
    "r7Z3zFSWqjy6mcyp7PvlImDMYzWHYYdncEaeBHd13TnTQ++AtY3Da2lu7WG5VlcV\n"
    "vcRaNx8ZLIUpW7u76F9fH25GFnQdF87dgt/ufntts4NifwRRRdIDAgunQ0Nf/TDy\n"
    "ow/P7dobloGl9c5xGyXk9EbYYiK/Iuga1yUKJP3UQhfOTNBsDkyUD/jLg+0bI8T+\n"
    "Vv4hKNhzUObXjzA5P7+RoEqnPe8R5wjvea6OJc2MDm5GFrzZOdIEnW/iuaQTE3ol\n"
    "PYZVDvgPB+l7IC0brwTvdnvXaGLFu8ICQtNoOuSkbEAysyBWCEGwloYLL3r/0ozb\n"
    "k3z5mUZVBwhpQS/GEChfUKiLxk9rbDmnhKASa6FVtycSVfoW/Oh5M65ARf9CFIUv\n"
    "cvDYLpQl7Ao8QiDqlQBvitNi665uucsZ/zOdFlTKKAellLASpB/q2Y6J4wkogFNa\n"
    "dGRV0WpQkcuMjXK7bzSSLMo0nMCm\n"
    "-----END CERTIFICATE-----\n";

static constexpr char HOST_KEY[] =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "MIIJJwIBAAKCAgEAzO6/HGJ46PIogZJPxfSE6eHuqE1JvC8eaSCOKpqbfGsIHPfh\n"
    "8d+TFatDe4GF4B6YYYmxpJUN85wZC+6GTA7xeM9UubssiwfACE/se5lJA0SSbXo9\n"
    "SXkE5sI/LM7NA6+h3PTmoLlwduWHh6twHbcfcJUtSOSJemCLUqZOiSQ+CGW2RzEF\n"
    "01SM/eOmpN9gtmVejD7wOxItbjX/DDE/IKv6xQ2G1RYxEpggCnNQjZj4R0uroN6L\n"
    "tM7hiQebueXs3KfVIEcEV9oXfTf2tI4G3NTn9MJUvZQNIMUVOtjl8UmLHfk3T9Hc\n"
    "eL/RM0svWIPWNoUJxA6m4u4g9D0eED2W0chKe86tF65RRYHvPNQXZf+tG7wyvRKP\n"
    "ePWIlEzIAgXW2WwXEvzFvvRnfX1mYeWA7KhyRaVcCp9H0i5ZP0L8E2RsvYd+2QMm\n"
    "zrxkeM01KMJSTAtUhqydNaH5dzlp8UXWvoTFLAh4F744OjdOyjHqoTZi1oh+iqsc\n"
    "3o4tWQF3YzpjharP3mngGj+YDp/ZN6F8QdFQU+iNFKx6aDRgnKm+ri0/yTDMQ3G1\n"
    "4bQ9FNmLRwB+W5UI9Oy931JxxcW7LxncbHgA326++bD1CdLXyxOvpHK40B5f9zsg\n"
    "m980xdjBcLr2o0nGAsZ9V79a0BNqjvDA3DjmXBGTYxJSpMGfipP5KbP2hl0CAwEA\n"
    "AQKCAgBSqV63EVVaCQujsCOzYn0WZgbBJmO+n3bxyqrtrm1XU0jzfl1KFfebPvi6\n"
    "YbVhgJXQihz4mRMGl4lW0cCj/0cRhvfS7xf5gIfKEor+FAdqZQd3V15PO5xphCK9\n"
    "bTEu8nIk0TgRzpr5qn3vkIxpwArTe6jHhT+a+ERaczCsiszm0DglITYLV0iDxIbc\n"
    "bCnziJIJmf2Gpj9i/C7DeT3QbO56+4jOfOQQbwJFlNwCMZi8EV7KRdoudWBtyH7d\n"
    "DkxreNsz6NFsqlDdNmyxybQk8VAa3yQVUBm3hSeaFBE0MYkG7xaLgMgggKbevM39\n"
    "Mzh9x034IjzYvlrWiayNunoSZmr8KhYQUsgAE5F+khTLfheiGvXLkjTdBLIGG5q7\n"
    "nb3G1la/jx0/0YpQvawNriovwii76cgHjmnEiNBJH685fvVP1y3bM/dJ62xdzFpw\n"
    "7/1xrii1D9rSvrns037WOrv6VtdtNpJoLbUlNXU8CX9Oc7umbBLCLEp00+0DDctk\n"
    "3KVX+sQc37w9KRURu8LqyFFx4VvyIQ+sJQKwVqFpaIkR7MzN6apkBCXwF+WtgtCz\n"
    "7RGcu8V00yA0Rqqm1RVhPzbBU5UII0sRTYDacZFqzku8dVqEH0dUGlvtG2oIu0ed\n"
    "OOv93q2EIyxmA88xcA2YFLI7P/qjYuhcUCd4QZb8S6/c71V+lQKCAQEA+fhQHE8j\n"
    "Pp45zNZ9ITBTWqf0slC9iafpZ1qbnI60Sm1UigOD6Qzc5p6jdKggNr7VcQK+QyOo\n"
    "pnM3qePHXgDzJZfdN7cXB9O7jZSTWUyTDc1C59b3H4ney/tIIj4FxCXBolkPrUOW\n"
    "PE96i2PRQRGI1Pp5TFcFcaT5ZhX9WTPo/0fYBFEpof62mxGYRbQ8cEmRQYAXmlES\n"
    "mqE9RwhwqTKy3VET0qDV5eq1EihMkWlDleyIUHF7Ra1ECzh8cbTx8NpS2Iu3BL5v\n"
    "Sk6fGv8aegmf4DZcDU4eoZXBA9veAigT0X5cQH4HsAuppIOfRD8pFaJEe2K5EXXu\n"
    "lDSlJk5BjSrARwKCAQEA0eBORzH+g2kB/I8JIMYqy4urI7mGU9xNSwF34HhlPURQ\n"
    "NxPIByjUKrKZnCVaN2rEk8vRzY8EdsxRpKeGBYW1DZ/NL8nM7RHMKjGopf/n2JRk\n"
    "7m1Mn6f4mLRIVzOID/eR2iGPvWNcKrJxgkGXNXlWEi0wasPwbVksV05uGd/5K/sT\n"
    "cVVKkYLPhcGkYd7VvCfgF5x+5kPRLTrG8EKbePclUAVhs9huSRR9FAjSThpLyJZo\n"
    "0/3WxPNm9FoPYGyAevvdPiF5f6yp1IdPDNZHx46WSVfkgqA5XICjanl5WCNHezDp\n"
    "93r/Ix7zJ0Iyu9BxI2wJH3wGGqmsGveKOfZl90MaOwKCAQBCSGfluc5cslQdTtrL\n"
    "TCcuKM8n4WUA9XdcopgUwXppKeh62EfIKlMBDBvHuTUhjyTF3LZa0z/LM04VTIL3\n"
    "GEVhOI2+UlxXBPv8pOMVkMqFpGITW9sXj9V2PWF5Qv0AcAqSZA9WIE/cGi8iewtn\n"
    "t6CS6P/1EDYvVlGTkk0ltDAaURCkxGjHveTp5ZZ9FTfZhohv1+lqUAkg25SGG2TU\n"
    "WM85BGC/P0q4tq3g7LKw9DqprJjQy+amKTWbzBSjihmFhj7lkNas+VpFV+e0nuSE\n"
    "a7zrFT7/gDF7I1yVC14pMDthF6Kar1CWi+El8Ijw7daVF/wUw67TRHRI9FS+fY3A\n"
    "Qw/NAoIBAFbE7LgElFwSEu8u17BEHbdPhC7d6gpLv2zuK3iTbg+5aYyL0hwbpjQM\n"
    "6PMkgjr9Gk6cap4YrdjLuklftUodMHB0i+lg/idZP1aGd1pCBcGGAICOkapEUMQZ\n"
    "bPsYY/1t9k//piS/qoBAjCs1IOXLx2j2Y9kQLxuWTX2/AEgUUDj9sdkeURj9wvxi\n"
    "xaps7WK//ablXZWnnhib/1mfwBVv4G5H+0/WgCoYnWmmCASgXIqOnMJgZOXCV+NY\n"
    "RJkx4qB19s9UGZ5ObVxfoLAG+2AmtD2YZ/IVegGjcWx40lE9LLVi0KgvosILbq3h\n"
    "cYYytEPXy6HHreJiGbSAeRZjp15l0LcCggEAWjeKaf61ogHq0uCwwipVdAGY4lNG\n"
    "dAh2IjAWX0IvjBuRVuUNZyB8GApH3qilDsuzPLYtIhHtbuPrFjPyxMBoppk7hLJ+\n"
    "ubzoHxMKeyKwlIi4jY+CXLaHtIV/hZFVYcdvr5A2zUrMgzEnDEEFn96+Dz0IdGrF\n"
    "a37oDKDYpfK8EFk/jb2aAhIgYSHSUg4KKlQRuPfu0Vt/O8aasjmAQvfEmbx6c2C5\n"
    "4q16Ky/ZM7mCqQNJAXgyPfOeJnX1PwCKntdEs2xtzXb7dEP9Yy9uAgmmVe7EzPCj\n"
    "ml57PWxIJKtok73AHEat6qboncHvW1RPDAiQYmXdbe40v4wlPDrnWjUUiA==\n"
    "-----END RSA PRIVATE KEY-----\n";

static constexpr char VALID_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIF+jCCA+KgAwIBAgICEAAwDQYJKoZIhvcNAQELBQAwgZcxCzAJBgNVBAYTAlVT\n"
    "MRMwEQYDVQQIDApDYWxpZm9ybmlhMRUwEwYDVQQKDAxTYW5kc3Rvcm0uaW8xGzAZ\n"
    "BgNVBAsMElRlc3RpbmcgRGVwYXJ0bWVudDEbMBkGA1UEAwwSaW50LWNhLmV4YW1w\n"
    "bGUuY29tMSIwIAYJKoZIhvcNAQkBFhNnYXJwbHlAc2FuZHN0b3JtLmlvMCAXDTIw\n"
    "MDYyNzAwNDI1M1oYDzIxMjAwNjI3MDA0MjUzWjCBkDELMAkGA1UEBhMCVVMxEzAR\n"
    "BgNVBAgMCkNhbGlmb3JuaWExFTATBgNVBAoMDFNhbmRzdG9ybS5pbzEbMBkGA1UE\n"
    "CwwSVGVzdGluZyBEZXBhcnRtZW50MRQwEgYDVQQDDAtleGFtcGxlLmNvbTEiMCAG\n"
    "CSqGSIb3DQEJARYTZ2FycGx5QHNhbmRzdG9ybS5pbzCCAiIwDQYJKoZIhvcNAQEB\n"
    "BQADggIPADCCAgoCggIBAMzuvxxieOjyKIGST8X0hOnh7qhNSbwvHmkgjiqam3xr\n"
    "CBz34fHfkxWrQ3uBheAemGGJsaSVDfOcGQvuhkwO8XjPVLm7LIsHwAhP7HuZSQNE\n"
    "km16PUl5BObCPyzOzQOvodz05qC5cHblh4ercB23H3CVLUjkiXpgi1KmTokkPghl\n"
    "tkcxBdNUjP3jpqTfYLZlXow+8DsSLW41/wwxPyCr+sUNhtUWMRKYIApzUI2Y+EdL\n"
    "q6Dei7TO4YkHm7nl7Nyn1SBHBFfaF3039rSOBtzU5/TCVL2UDSDFFTrY5fFJix35\n"
    "N0/R3Hi/0TNLL1iD1jaFCcQOpuLuIPQ9HhA9ltHISnvOrReuUUWB7zzUF2X/rRu8\n"
    "Mr0Sj3j1iJRMyAIF1tlsFxL8xb70Z319ZmHlgOyockWlXAqfR9IuWT9C/BNkbL2H\n"
    "ftkDJs68ZHjNNSjCUkwLVIasnTWh+Xc5afFF1r6ExSwIeBe+ODo3Tsox6qE2YtaI\n"
    "foqrHN6OLVkBd2M6Y4Wqz95p4Bo/mA6f2TehfEHRUFPojRSsemg0YJypvq4tP8kw\n"
    "zENxteG0PRTZi0cAfluVCPTsvd9SccXFuy8Z3Gx4AN9uvvmw9QnS18sTr6RyuNAe\n"
    "X/c7IJvfNMXYwXC69qNJxgLGfVe/WtATao7wwNw45lwRk2MSUqTBn4qT+Smz9oZd\n"
    "AgMBAAGjUzBRMB0GA1UdDgQWBBRdpPVLjMnJFs7lCtMW6x39Wnwt3TAfBgNVHSME\n"
    "GDAWgBTZxw81mTQuaT+p41J39QTF7kCp5zAPBgNVHRMBAf8EBTADAQH/MA0GCSqG\n"
    "SIb3DQEBCwUAA4ICAQA2kid2fGqjeDRVuclfDRr0LhbFYfJJXxW7SPgcUpJYXeAz\n"
    "LXotBm/Cc+K01nNtl0JYfJy4IkaQUYgfVsA5/FqTGnbRmpEd5XidiGE6PfkXZSNj\n"
    "02v6Uv2bAs8NnJirS5F0JhWZ45xAOMbl04QPUdkISF1JzioCcCqWOggbIV7kzwrB\n"
    "MTcsx8vuh04S9vB318pKli4uIjNdwu7HnoqbrqhSgTUK1aXS6sDQNN/nvR6F5TRL\n"
    "MC0cCtIA6n04c09WRfHxl/YPQwayxGD23eQ9UC7Noe/R8B3K/+6XUYIEmXx6HpnP\n"
    "yt/79iBPwLnaVjGDwKfI8EPuSo7AkDSO3uxMjf7eCL2sCzWlgsne9yOfYxGn+q9K\n"
    "h3KTOR1b7EVU/G4h7JxlSHqf3Ii9qFba/HsUo1yMjVEraMNpxXCijGsN30fqUpLg\n"
    "2g9lNKmIdyHuYdlZET082b1dvb7cfYChlqHvrPv5awbsc1Ka2pOFOMwnUi2w3cHc\n"
    "TLq3SyipI+sgJg3HHSJ3zVHrgKUeDoQi52i5WedvIBPfHC4Ik4zEzlkCMDd3hoLe\n"
    "THAsBGBBEICK+lLSo0Mst2kxhTHHK+PxmhorXXnGOpla2wbwzVZgxur45axCWVqH\n"
    "cbdcVhzR2w4XhjaS74WGiHHn5mHb/uYZJiZGpFCNefU2WOBlRCX7hgAzlPa4ZQ==\n"
    "-----END CERTIFICATE-----\n";

static constexpr char HOST_KEY2[] =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "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\n"
    "-----END RSA PRIVATE KEY-----\n";

static constexpr char VALID_CERT2[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIF+jCCA+KgAwIBAgICEAAwDQYJKoZIhvcNAQELBQAwgZcxCzAJBgNVBAYTAlVT\n"
    "MRMwEQYDVQQIDApDYWxpZm9ybmlhMRUwEwYDVQQKDAxTYW5kc3Rvcm0uaW8xGzAZ\n"
    "BgNVBAsMElRlc3RpbmcgRGVwYXJ0bWVudDEbMBkGA1UEAwwSaW50LWNhLmV4YW1w\n"
    "bGUuY29tMSIwIAYJKoZIhvcNAQkBFhNnYXJwbHlAc2FuZHN0b3JtLmlvMCAXDTIw\n"
    "MDYyNzAwNDI1M1oYDzIxMjAwNjI3MDA0MjUzWjCBkDELMAkGA1UEBhMCVVMxEzAR\n"
    "BgNVBAgMCkNhbGlmb3JuaWExFTATBgNVBAoMDFNhbmRzdG9ybS5pbzEbMBkGA1UE\n"
    "CwwSVGVzdGluZyBEZXBhcnRtZW50MRQwEgYDVQQDDAtleGFtcGxlLm5ldDEiMCAG\n"
    "CSqGSIb3DQEJARYTZ2FycGx5QHNhbmRzdG9ybS5pbzCCAiIwDQYJKoZIhvcNAQEB\n"
    "BQADggIPADCCAgoCggIBALaVC2wVXHj+VEz/AKv6meM7sLNJoH0Ws5RRQ859EqcS\n"
    "kYH1ETqwwfBDHH3y/iMVmHc7M2J5u0eKfLnUB7anV9ZviaVjyHBMjhrihIxBhlQW\n"
    "37XohrwmWh5poUhhbgbkUJ7JjjDdrF+AEz4CEnlKSs92HBMOc2xlfV73kAQogmWC\n"
    "ThGNkas7s38Jju+jX/9KULRWjer4xzhjqeXusRFlDTK7pYsPN/wWJrThNAh/UJi0\n"
    "ouXf7DjLKaV1XbsFLIR79oMKHFxYpOBVIQzCo386e9br0S6gJ6bH+rubkoV8KoGp\n"
    "qIUHLYfQ9MeSjKE8JX+y51ednSeBadJ6eBWJDYSScajF4ybs1uB6h8rf883pYV2r\n"
    "ysj8n1xppER80CwGjswzft6lwbwK7m2GqbsvwhdZ/w4fTr3BifLytsLWoRooNlq8\n"
    "IeZNHmRIkH5BbVyoVZsTYHBBK+nKCDwD3vPNhdB61oDOYf4bkudDAuTlso5wMkme\n"
    "y/0I9O4w3zXIo9cmo5r+GobC/BSOIWpfJNw0l3tzA5wMJgcSoPm/ylFjYrnHSJ2V\n"
    "Gde3Oe+uWkKHiD2RVQaAEK4BRSzxN9cAu0sKzLijIctlCm4F43GeTDJG0tlgUmqg\n"
    "RS0srMx2NHBTwkYeiu3e7w70hu5WU9epTRX2oG73DtbrrEAQ9toBrzThds2Xt++x\n"
    "AgMBAAGjUzBRMB0GA1UdDgQWBBSCVMitc7axjQ0JObyQ7SoZ15v41jAfBgNVHSME\n"
    "GDAWgBTZxw81mTQuaT+p41J39QTF7kCp5zAPBgNVHRMBAf8EBTADAQH/MA0GCSqG\n"
    "SIb3DQEBCwUAA4ICAQAGqI+GGbSHkV9C16OLKgujS17zAJDuMeUZVoUvsh0oj7hK\n"
    "QwuJ6M6VIWZXk0Ccs/TbtQgyUtt98HY/M5LYjvuB3jb348TvYvBg1un6DC1LNFnw\n"
    "x19eUvwxhoI0I9A/heD6251plaXl0rk+wmTn+gqHNswb0LZw7l8XclOQ8s13/Ei3\n"
    "fD4P5N3LiXaPfcXzFtEvWJE1ONC/PvLfwWWE2T+/LabJ4I4iumX8oAJZyx9BCE09\n"
    "54/0cV1V6xjp31/CS7vkYtDMeREnydwC3PsjjzO18nM0GVw6R2eok/yvD2Rg/pqJ\n"
    "CiscKswcy0OR42pCzJyAwHaXV0KZEG9E97ukiqh3ByBUfR0ZwkKv7tDaL7UQiXdF\n"
    "sheJ3l8TyQNcuWljjm1MWJt9ZzZt5zE4+yes4YVDNNe9l2jIoT8641pcy2MmPOdJ\n"
    "8pEE3xJ2SAdeJKVXuHoi7glzmlK1O5nSNK3GIfKRwJ2hmIXSAoMPfpwtJWdJDNGZ\n"
    "N2HThXDMleMrJqwsdToRCp0nBm40cKSDk/o7SfiE7z4e1EVDAFBlWl5SAq9Pqwh5\n"
    "lBlsQXd5SbzWGyVk7BjtT3ttbXru9NEINo1l9Cw74GQuW40FsQf4drZVDVtaNWPd\n"
    "IvZM211bcU/zZV44rkz3nc08jSGo2qP8bEcuYAlTneDLyrAmpisUXKYm1Q1SxA==\n"
    "-----END CERTIFICATE-----\n";

static constexpr char EXPIRED_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIF+DCCA+CgAwIBAgICEAAwDQYJKoZIhvcNAQELBQAwgZcxCzAJBgNVBAYTAlVT\n"
    "MRMwEQYDVQQIDApDYWxpZm9ybmlhMRUwEwYDVQQKDAxTYW5kc3Rvcm0uaW8xGzAZ\n"
    "BgNVBAsMElRlc3RpbmcgRGVwYXJ0bWVudDEbMBkGA1UEAwwSaW50LWNhLmV4YW1w\n"
    "bGUuY29tMSIwIAYJKoZIhvcNAQkBFhNnYXJwbHlAc2FuZHN0b3JtLmlvMB4XDTE2\n"
    "MDEwMTAwMDAwMFoXDTE2MDEwMTAwMDAwMFowgZAxCzAJBgNVBAYTAlVTMRMwEQYD\n"
    "VQQIDApDYWxpZm9ybmlhMRUwEwYDVQQKDAxTYW5kc3Rvcm0uaW8xGzAZBgNVBAsM\n"
    "ElRlc3RpbmcgRGVwYXJ0bWVudDEUMBIGA1UEAwwLZXhhbXBsZS5jb20xIjAgBgkq\n"
    "hkiG9w0BCQEWE2dhcnBseUBzYW5kc3Rvcm0uaW8wggIiMA0GCSqGSIb3DQEBAQUA\n"
    "A4ICDwAwggIKAoICAQDM7r8cYnjo8iiBkk/F9ITp4e6oTUm8Lx5pII4qmpt8awgc\n"
    "9+Hx35MVq0N7gYXgHphhibGklQ3znBkL7oZMDvF4z1S5uyyLB8AIT+x7mUkDRJJt\n"
    "ej1JeQTmwj8szs0Dr6Hc9OaguXB25YeHq3Adtx9wlS1I5Il6YItSpk6JJD4IZbZH\n"
    "MQXTVIz946ak32C2ZV6MPvA7Ei1uNf8MMT8gq/rFDYbVFjESmCAKc1CNmPhHS6ug\n"
    "3ou0zuGJB5u55ezcp9UgRwRX2hd9N/a0jgbc1Of0wlS9lA0gxRU62OXxSYsd+TdP\n"
    "0dx4v9EzSy9Yg9Y2hQnEDqbi7iD0PR4QPZbRyEp7zq0XrlFFge881Bdl/60bvDK9\n"
    "Eo949YiUTMgCBdbZbBcS/MW+9Gd9fWZh5YDsqHJFpVwKn0fSLlk/QvwTZGy9h37Z\n"
    "AybOvGR4zTUowlJMC1SGrJ01ofl3OWnxRda+hMUsCHgXvjg6N07KMeqhNmLWiH6K\n"
    "qxzeji1ZAXdjOmOFqs/eaeAaP5gOn9k3oXxB0VBT6I0UrHpoNGCcqb6uLT/JMMxD\n"
    "cbXhtD0U2YtHAH5blQj07L3fUnHFxbsvGdxseADfbr75sPUJ0tfLE6+kcrjQHl/3\n"
    "OyCb3zTF2MFwuvajScYCxn1Xv1rQE2qO8MDcOOZcEZNjElKkwZ+Kk/kps/aGXQID\n"
    "AQABo1MwUTAdBgNVHQ4EFgQUXaT1S4zJyRbO5QrTFusd/Vp8Ld0wHwYDVR0jBBgw\n"
    "FoAU2ccPNZk0Lmk/qeNSd/UExe5AqecwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG\n"
    "9w0BAQsFAAOCAgEAYRQazCV707BpbBo3j2PXfg4rmrm1GIA0JXFsY27CII0aSgTw\n"
    "roMBnwp3sJ+UIxqddkwf/4Bn/kq8yHu8WMc1cb4bzsqgU4K2zGJcVF3i9Y4R6oE1\n"
    "9Y6QM1db5HYiCdXSNW5uZUQGButyIXsUfPns0jMZfmEhsW4WrN3m2qE357FeBfCF\n"
    "nP4Ij3sbUq01OoPBL6sWUbltfL5PgqKitE6UFu1/WFpBatP+8ITOOLhkGmJ70zb1\n"
    "rY3jnwlEaRJVw8DmIkkabrgKu2gUZ3qKX9aeW9iJW524A2ZjEg1xoBRq9MVcfiRN\n"
    "qAERaEd7MgjIFmOYR5O2juzR3eMrv+U+JsY6K5ItTwwE/MHKjYRM22CgmQahPfvo\n"
    "o40qLUn/zJNJAN+1hrmOqFXpC5vmfKV9pcG7BOsuZ9V9gkssnJRosCuU8iIW3gyo\n"
    "C6TFLIneStvBzokoCTv0Fxxh/vqfIWmHYr7nsFM8S/X2iDLuCoiB6qsRw5NaOIg6\n"
    "QB7cEi3sgBZU+eJDmynR3waIU0HGmj9DK8Tc2TMd5wvkBpJBkqqQkICVQ/u/g7up\n"
    "swvT5Iap509sI4nmKqe9meN6m3xBSJrCNPTbjUyu7PuC/rBe7lVwnP5/PN/aj6ZU\n"
    "XGyLwArQ/5GgT2sy3aEQQTtb+kthnZo7NL8nmkpoTbrm84DJ4dwkD4qc+hs=\n"
    "-----END CERTIFICATE-----\n";

static constexpr char SELF_SIGNED_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIGLTCCBBWgAwIBAgIUIGB2OqfFvs22f6uTwFJwWKaLH+kwDQYJKoZIhvcNAQEL\n"
    "BQAwgaQxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRIwEAYDVQQH\n"
    "DAlQYWxvIEFsdG8xFTATBgNVBAoMDFNhbmRzdG9ybS5pbzEbMBkGA1UECwwSVGVz\n"
    "dGluZyBEZXBhcnRtZW50MRQwEgYDVQQDDAtleGFtcGxlLmNvbTEiMCAGCSqGSIb3\n"
    "DQEJARYTZ2FycGx5QHNhbmRzdG9ybS5pbzAgFw0yMDA2MjcwMDQyNTNaGA8yMTIw\n"
    "MDYyNzAwNDI1M1owgaQxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlh\n"
    "MRIwEAYDVQQHDAlQYWxvIEFsdG8xFTATBgNVBAoMDFNhbmRzdG9ybS5pbzEbMBkG\n"
    "A1UECwwSVGVzdGluZyBEZXBhcnRtZW50MRQwEgYDVQQDDAtleGFtcGxlLmNvbTEi\n"
    "MCAGCSqGSIb3DQEJARYTZ2FycGx5QHNhbmRzdG9ybS5pbzCCAiIwDQYJKoZIhvcN\n"
    "AQEBBQADggIPADCCAgoCggIBAMzuvxxieOjyKIGST8X0hOnh7qhNSbwvHmkgjiqa\n"
    "m3xrCBz34fHfkxWrQ3uBheAemGGJsaSVDfOcGQvuhkwO8XjPVLm7LIsHwAhP7HuZ\n"
    "SQNEkm16PUl5BObCPyzOzQOvodz05qC5cHblh4ercB23H3CVLUjkiXpgi1KmTokk\n"
    "PghltkcxBdNUjP3jpqTfYLZlXow+8DsSLW41/wwxPyCr+sUNhtUWMRKYIApzUI2Y\n"
    "+EdLq6Dei7TO4YkHm7nl7Nyn1SBHBFfaF3039rSOBtzU5/TCVL2UDSDFFTrY5fFJ\n"
    "ix35N0/R3Hi/0TNLL1iD1jaFCcQOpuLuIPQ9HhA9ltHISnvOrReuUUWB7zzUF2X/\n"
    "rRu8Mr0Sj3j1iJRMyAIF1tlsFxL8xb70Z319ZmHlgOyockWlXAqfR9IuWT9C/BNk\n"
    "bL2HftkDJs68ZHjNNSjCUkwLVIasnTWh+Xc5afFF1r6ExSwIeBe+ODo3Tsox6qE2\n"
    "YtaIfoqrHN6OLVkBd2M6Y4Wqz95p4Bo/mA6f2TehfEHRUFPojRSsemg0YJypvq4t\n"
    "P8kwzENxteG0PRTZi0cAfluVCPTsvd9SccXFuy8Z3Gx4AN9uvvmw9QnS18sTr6Ry\n"
    "uNAeX/c7IJvfNMXYwXC69qNJxgLGfVe/WtATao7wwNw45lwRk2MSUqTBn4qT+Smz\n"
    "9oZdAgMBAAGjUzBRMB0GA1UdDgQWBBRdpPVLjMnJFs7lCtMW6x39Wnwt3TAfBgNV\n"
    "HSMEGDAWgBRdpPVLjMnJFs7lCtMW6x39Wnwt3TAPBgNVHRMBAf8EBTADAQH/MA0G\n"
    "CSqGSIb3DQEBCwUAA4ICAQDEEnq3/Yg0UL3d8z0EWXGnj5hbc8Cf9hj8J918aift\n"
    "XhqQ+EU6BV1V7IsrUZ07Wkws7hgd7HpBgmSNQu9cIoSyjiS9je8KL9TqwmBaPNg3\n"
    "jE31maqLHdLQfR0USYo7wp8cE7w/tojLwyhuwVEJR4IpVlfAgmD5HMhCX4vwZTUB\n"
    "bkzsRtY56JRhNDO2ExY7QPFF4FhXLf8eZGqqk09FTpQemJFwZ2+MYlSOILrP4RL3\n"
    "T9LW0EgymAjDUHT047xr5xPAjRUEplqT90bEsAp5D199m/c143tq3Cke/eQDWAR7\n"
    "HVYmRmuOhwyqhkKfssZZvuq7Shm0u2vuOvfGhcW7JmukalrCixjitQmbOv1EJT0F\n"
    "tQN61nRTUpnC37DEgtYpV8n+GgT1hWXDzC/0UNVOFIR26eX0kxZmqU6v+Rqz/qYe\n"
    "NA2TXZ4YvL081QvPFOWVpodM6LLYw2cSGBCdfAdRE1ECoqzk5EgRBH5SrZnuebMG\n"
    "V8aJsIRMI011QDEz69YJFzefI9WaawcHqfTWZoCeCBDNm0pEVqRbBAQ8E7IXfQUu\n"
    "WjPbENMyTTp6+uRmQkmNJjv9HcvFUu+wyhFODBrZ4LEwFP+oWBGWqru28Se7b66H\n"
    "ZvzKfQVXzYpEmhHHK2n5X1hNwSr0kb3QffVpbz3/TBIgQcOyBp/RnOY38pngfRw1\n"
    "SQ==\n"
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

  Promise<void> writeToServer(AsyncIoStream& client) {
    return client.write("foo", 4);
  }

  Promise<void> readFromClient(AsyncIoStream& server) {
    auto buf = heapArray<char>(4);

    auto readPromise = server.read(buf.begin(), buf.size());

    auto checkBuffer = [buf = kj::mv(buf)]() {
      KJ_ASSERT(kj::StringPtr(buf.begin(), buf.end()-1) == kj::StringPtr("foo"));
    };

    return readPromise.then(kj::mv(checkBuffer));
  }

  void testConnection(AsyncIoStream& client, AsyncIoStream& server) {
    auto writePromise = writeToServer(client);
    auto readPromise = readFromClient(server);

    writePromise.wait(io.waitScope);
    readPromise.wait(io.waitScope);
  };
};

KJ_TEST("TLS basics") {
  TlsTest test;
  ErrorNexus e;

  auto pipe = test.io.provider->newTwoWayPipe();

  auto clientPromise = e.wrap(test.tlsClient.wrapClient(kj::mv(pipe.ends[0]), "example.com"));
  auto serverPromise = e.wrap(test.tlsServer.wrapServer(kj::mv(pipe.ends[1])));

  auto client = clientPromise.wait(test.io.waitScope);
  auto server = serverPromise.wait(test.io.waitScope);

  test.testConnection(*client, *server);

  // Test clean shutdown.
  {
    auto eofPromise = server->readAllText();
    KJ_EXPECT(!eofPromise.poll(test.io.waitScope));
    client->shutdownWrite();
    KJ_ASSERT(eofPromise.poll(test.io.waitScope));
    KJ_EXPECT(eofPromise.wait(test.io.waitScope) == ""_kj);
  }

  // Test UNCLEAN shutdown in other direction.
  {
    auto eofPromise = client->readAllText();
    KJ_EXPECT(!eofPromise.poll(test.io.waitScope));
    { auto drop = kj::mv(server); }
    KJ_EXPECT(eofPromise.poll(test.io.waitScope));
    KJ_EXPECT_THROW(DISCONNECTED, eofPromise.wait(test.io.waitScope));
  }
}

KJ_TEST("TLS half-duplex") {
  // Test shutting down one direction of a connection but continuing to stream in the other
  // direction.

  TlsTest test;
  ErrorNexus e;

  auto pipe = test.io.provider->newTwoWayPipe();

  auto clientPromise = e.wrap(test.tlsClient.wrapClient(kj::mv(pipe.ends[0]), "example.com"));
  auto serverPromise = e.wrap(test.tlsServer.wrapServer(kj::mv(pipe.ends[1])));

  auto client = clientPromise.wait(test.io.waitScope);
  auto server = serverPromise.wait(test.io.waitScope);

  client->shutdownWrite();
  KJ_EXPECT(server->readAllText().wait(test.io.waitScope) == "");

  for (uint i = 0; i < 100; i++) {
    char buffer[7];
    auto writePromise = server->write("foobar", 6);
    auto readPromise = client->read(buffer, 6);
    writePromise.wait(test.io.waitScope);
    readPromise.wait(test.io.waitScope);
    buffer[6] = '\0';
    KJ_ASSERT(kj::StringPtr(buffer, 6) == "foobar");
  }

  server->shutdownWrite();
  KJ_EXPECT(client->readAllText().wait(test.io.waitScope) == "");
}

KJ_TEST("TLS peer identity") {
  TlsTest test;
  ErrorNexus e;

  auto pipe = test.io.provider->newTwoWayPipe();

  auto innerClientId = kj::LocalPeerIdentity::newInstance({});
  auto& innerClientIdRef = *innerClientId;
  auto clientPromise = e.wrap(test.tlsClient.wrapClient(
      kj::AuthenticatedStream { kj::mv(pipe.ends[0]), kj::mv(innerClientId) },
      "example.com"));

  auto innerServerId = kj::LocalPeerIdentity::newInstance({});
  auto& innerServerIdRef = *innerServerId;
  auto serverPromise = e.wrap(test.tlsServer.wrapServer(
    kj::AuthenticatedStream { kj::mv(pipe.ends[1]), kj::mv(innerServerId) }));

  auto client = clientPromise.wait(test.io.waitScope);
  auto server = serverPromise.wait(test.io.waitScope);

  {
    auto id = client.peerIdentity.downcast<TlsPeerIdentity>();
    KJ_ASSERT(id->hasCertificate());
    KJ_EXPECT(id->getCommonName() == "example.com");
    KJ_EXPECT(&id->getNetworkIdentity() == &innerClientIdRef);
    KJ_EXPECT(id->toString() == "example.com");
  }

  {
    auto id = server.peerIdentity.downcast<TlsPeerIdentity>();
    KJ_EXPECT(!id->hasCertificate());
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE(
        "client did not provide a certificate", id->getCommonName());
    KJ_EXPECT(&id->getNetworkIdentity() == &innerServerIdRef);
    KJ_EXPECT(id->toString() == "(anonymous client)");
  }

  test.testConnection(*client.stream, *server.stream);
}

KJ_TEST("TLS multiple messages") {
  TlsTest test;
  ErrorNexus e;

  auto pipe = test.io.provider->newTwoWayPipe();

  auto clientPromise = e.wrap(test.tlsClient.wrapClient(kj::mv(pipe.ends[0]), "example.com"));
  auto serverPromise = e.wrap(test.tlsServer.wrapServer(kj::mv(pipe.ends[1])));

  auto client = clientPromise.wait(test.io.waitScope);
  auto server = serverPromise.wait(test.io.waitScope);

  auto writePromise = client->write("foo", 3)
      .then([&]() { return client->write("bar", 3); });

  char buf[4];
  buf[3] = '\0';

  server->read(&buf, 3).wait(test.io.waitScope);
  KJ_ASSERT(kj::StringPtr(buf) == "foo");

  writePromise = writePromise
      .then([&]() { return client->write("baz", 3); });

  server->read(&buf, 3).wait(test.io.waitScope);
  KJ_ASSERT(kj::StringPtr(buf) == "bar");

  server->read(&buf, 3).wait(test.io.waitScope);
  KJ_ASSERT(kj::StringPtr(buf) == "baz");

  auto readPromise = server->read(&buf, 3);
  KJ_EXPECT(!readPromise.poll(test.io.waitScope));

  writePromise = writePromise
      .then([&]() { return client->write("qux", 3); });

  readPromise.wait(test.io.waitScope);
  KJ_ASSERT(kj::StringPtr(buf) == "qux");
}

KJ_TEST("TLS zero-sized write") {
  TlsTest test;
  ErrorNexus e;

  auto pipe = test.io.provider->newTwoWayPipe();

  auto clientPromise = e.wrap(test.tlsClient.wrapClient(kj::mv(pipe.ends[0]), "example.com"));
  auto serverPromise = e.wrap(test.tlsServer.wrapServer(kj::mv(pipe.ends[1])));

  auto client = clientPromise.wait(test.io.waitScope);
  auto server = serverPromise.wait(test.io.waitScope);

  char buf[7];
  auto readPromise = server->read(&buf, 6);

  client->write("", 0).wait(test.io.waitScope);
  client->write("foo", 3).wait(test.io.waitScope);
  client->write("", 0).wait(test.io.waitScope);
  client->write("bar", 3).wait(test.io.waitScope);

  readPromise.wait(test.io.waitScope);
  buf[6] = '\0';

  KJ_ASSERT(kj::StringPtr(buf) == "foobar");
}

kj::Promise<void> writeN(kj::AsyncIoStream& stream, kj::StringPtr text, size_t count) {
  if (count == 0) return kj::READY_NOW;
  --count;
  return stream.write(text.begin(), text.size())
      .then([&stream, text, count]() {
    return writeN(stream, text, count);
  });
}

kj::Promise<void> readN(kj::AsyncIoStream& stream, kj::StringPtr text, size_t count) {
  if (count == 0) return kj::READY_NOW;
  --count;
  auto buf = kj::heapString(text.size());
  auto promise = stream.read(buf.begin(), buf.size());
  return promise.then([&stream, text, buf=kj::mv(buf), count]() {
    KJ_ASSERT(buf == text, buf, text, count);
    return readN(stream, text, count);
  });
}

KJ_TEST("TLS full duplex") {
  TlsTest test;
  ErrorNexus e;

  auto pipe = test.io.provider->newTwoWayPipe();

#if _WIN32
  // On Windows we observe that `writeUp`, below, completes before the other end has started
  // reading, failing the `!writeUp.poll()` expectation. I guess Windows has big buffers. We can
  // fix this by requesting small buffers here. (Worth keeping in mind that Windows doesn't have
  // socketpairs, so `newTwoWayPipe()` is implemented in terms of loopback TCP, ugh.)
  uint small = 256;
  pipe.ends[0]->setsockopt(SOL_SOCKET, SO_SNDBUF, &small, sizeof(small));
  pipe.ends[0]->setsockopt(SOL_SOCKET, SO_RCVBUF, &small, sizeof(small));
#endif

  auto clientPromise = e.wrap(test.tlsClient.wrapClient(kj::mv(pipe.ends[0]), "example.com"));
  auto serverPromise = e.wrap(test.tlsServer.wrapServer(kj::mv(pipe.ends[1])));

  auto client = clientPromise.wait(test.io.waitScope);
  auto server = serverPromise.wait(test.io.waitScope);

  auto writeUp = writeN(*client, "foo", 10000);
  auto readDown = readN(*client, "bar", 10000);
#if !(_WIN32 && __clang__)
  // TODO(someday): work out why this expectation fails even with the above fix
  KJ_EXPECT(!writeUp.poll(test.io.waitScope));
#endif
  KJ_EXPECT(!readDown.poll(test.io.waitScope));

  auto writeDown = writeN(*server, "bar", 10000);
  auto readUp = readN(*server, "foo", 10000);

  readUp.wait(test.io.waitScope);
  readDown.wait(test.io.waitScope);
  writeUp.wait(test.io.waitScope);
  writeDown.wait(test.io.waitScope);
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

  TlsTest test(TlsTest::defaultClient(), kj::mv(serverOptions));
  ErrorNexus e;

  auto pipe = test.io.provider->newTwoWayPipe();

  auto clientPromise = e.wrap(test.tlsClient.wrapClient(kj::mv(pipe.ends[0]), "example.com"));
  auto serverPromise = e.wrap(test.tlsServer.wrapServer(kj::mv(pipe.ends[1])));

  auto client = clientPromise.wait(test.io.waitScope);
  auto server = serverPromise.wait(test.io.waitScope);

  test.testConnection(*client, *server);

  KJ_ASSERT(callback.callCount == 1);
}

void expectInvalidCert(kj::StringPtr hostname, TlsCertificate cert,
                       kj::StringPtr message, kj::Maybe<kj::StringPtr> altMessage = nullptr) {
  TlsKeypair keypair = { TlsPrivateKey(HOST_KEY), kj::mv(cert) };
  TlsContext::Options serverOpts;
  serverOpts.defaultKeypair = keypair;
  TlsTest test(TlsTest::defaultClient(), kj::mv(serverOpts));
  ErrorNexus e;

  auto pipe = test.io.provider->newTwoWayPipe();

  auto clientPromise = e.wrap(test.tlsClient.wrapClient(kj::mv(pipe.ends[0]), hostname));
  auto serverPromise = e.wrap(test.tlsServer.wrapServer(kj::mv(pipe.ends[1])));

  clientPromise.then([](kj::Own<kj::AsyncOutputStream>) {
    KJ_FAIL_EXPECT("expected exception");
  }, [message, altMessage](kj::Exception&& e) {
    if (kj::_::hasSubstring(e.getDescription(), message)) {
      return;
    }

    KJ_IF_MAYBE(a, altMessage) {
      if (kj::_::hasSubstring(e.getDescription(), *a)) {
        return;
      }
    }

    KJ_FAIL_EXPECT("exception didn't contain expected message", message,
        altMessage.orDefault(nullptr), e);
  }).wait(test.io.waitScope);
}

KJ_TEST("TLS certificate validation") {
  // Where we've given two possible error texts below, it's because OpenSSL v1 produces the former
  // text while v3 produces the latter. Note that as of this writing, our Windows CI build claims
  // to be v3 but produces v1 text, for reasons I don't care to investigate.
  expectInvalidCert("wrong.com", TlsCertificate(kj::str(VALID_CERT, INTERMEDIATE_CERT)),
                    "Hostname mismatch"_kj, "hostname mismatch"_kj);
  expectInvalidCert("example.com", TlsCertificate(VALID_CERT),
                    "unable to get local issuer certificate"_kj);
  expectInvalidCert("example.com", TlsCertificate(kj::str(EXPIRED_CERT, INTERMEDIATE_CERT)),
                    "certificate has expired"_kj);
  expectInvalidCert("example.com", TlsCertificate(SELF_SIGNED_CERT),
                    "self signed certificate"_kj, "self-signed certificate"_kj);
}

// BoringSSL seems to print error messages differently.
#ifdef OPENSSL_IS_BORINGSSL
#define SSL_MESSAGE_DIFFERENT_IN_BORINGSSL(interesting, boring) boring
#else
#define SSL_MESSAGE_DIFFERENT_IN_BORINGSSL(interesting, boring) interesting
#endif

KJ_TEST("TLS client certificate verification") {
  enum class VerifyClients {
    YES,
    NO
  };
  auto makeServerOptionsForClient = [](
      const TlsContext::Options& clientOptions,
      VerifyClients verifyClients
  ) {
    TlsContext::Options serverOptions = TlsTest::defaultServer();
    serverOptions.verifyClients = verifyClients == VerifyClients::YES;

    // Share the certs between the client and server.
    serverOptions.trustedCertificates = clientOptions.trustedCertificates;

    return serverOptions;
  };

  TlsKeypair selfSignedKeypair = { TlsPrivateKey(HOST_KEY), TlsCertificate(SELF_SIGNED_CERT) };
  TlsKeypair altKeypair = {
      TlsPrivateKey(HOST_KEY2),
      TlsCertificate(kj::str(VALID_CERT2, INTERMEDIATE_CERT)),
  };

  {
    // No certificate loaded in the client: fail
    auto clientOptions = TlsTest::defaultClient();
    auto serverOptions = makeServerOptionsForClient(clientOptions, VerifyClients::YES);
    TlsTest test(kj::mv(clientOptions), kj::mv(serverOptions));

    auto pipe = test.io.provider->newTwoWayPipe();

    auto clientPromise = test.tlsClient.wrapClient(kj::mv(pipe.ends[0]), "example.com")
        .then([](kj::Own<kj::AsyncIoStream> stream) {
      auto promise = stream->readAllBytes();
      return promise.attach(kj::mv(stream));
    });
    auto serverPromise = test.tlsServer.wrapServer(kj::mv(pipe.ends[1]));

    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE(
        SSL_MESSAGE_DIFFERENT_IN_BORINGSSL("peer did not return a certificate",
                                           "PEER_DID_NOT_RETURN_A_CERTIFICATE"),
        serverPromise.ignoreResult().wait(test.io.waitScope));
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE(
        SSL_MESSAGE_DIFFERENT_IN_BORINGSSL(
            "alert",  // "alert handshake failure" or "alert certificate required"
            "ALERT"), // "ALERT_HANDSHAKE_FAILURE" or "ALERT_CERTIFICATE_REQUIRED"
        clientPromise.ignoreResult().wait(test.io.waitScope));
  }

  {
    // Self-signed certificate loaded in the client: fail
    auto clientOptions = TlsTest::defaultClient();
    clientOptions.defaultKeypair = selfSignedKeypair;

    auto serverOptions = makeServerOptionsForClient(clientOptions, VerifyClients::YES);
    TlsTest test(kj::mv(clientOptions), kj::mv(serverOptions));

    auto pipe = test.io.provider->newTwoWayPipe();

    auto clientPromise = test.tlsClient.wrapClient(kj::mv(pipe.ends[0]), "example.com")
        .then([](kj::Own<kj::AsyncIoStream> stream) {
      auto promise = stream->readAllBytes();
      return promise.attach(kj::mv(stream));
    });
    auto serverPromise = test.tlsServer.wrapServer(kj::mv(pipe.ends[1]));

    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE(
        SSL_MESSAGE_DIFFERENT_IN_BORINGSSL("certificate verify failed",
                                           "CERTIFICATE_VERIFY_FAILED"),
        serverPromise.ignoreResult().wait(test.io.waitScope));
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE(
        SSL_MESSAGE_DIFFERENT_IN_BORINGSSL("alert unknown ca",
                                           "TLSV1_ALERT_UNKNOWN_CA"),
        clientPromise.ignoreResult().wait(test.io.waitScope));
  }

  {
    // Trusted certificate loaded in the client: success.
    auto clientOptions = TlsTest::defaultClient();
    clientOptions.defaultKeypair = altKeypair;

    auto serverOptions = makeServerOptionsForClient(clientOptions, VerifyClients::YES);
    TlsTest test(kj::mv(clientOptions), kj::mv(serverOptions));

    ErrorNexus e;

    auto pipe = test.io.provider->newTwoWayPipe();

    auto clientPromise = e.wrap(test.tlsClient.wrapClient(kj::mv(pipe.ends[0]), "example.com"));
    auto serverPromise = e.wrap(test.tlsServer.wrapServer(
        kj::AuthenticatedStream { kj::mv(pipe.ends[1]), kj::UnknownPeerIdentity::newInstance() }));

    auto client = clientPromise.wait(test.io.waitScope);
    auto server = serverPromise.wait(test.io.waitScope);

    auto id = server.peerIdentity.downcast<TlsPeerIdentity>();
    KJ_ASSERT(id->hasCertificate());
    KJ_EXPECT(id->getCommonName() == "example.net");

    test.testConnection(*client, *server.stream);
  }

  {
    // If verifyClients is off, client certificate is ignored, even if trusted.
    auto clientOptions = TlsTest::defaultClient();
    auto serverOptions = makeServerOptionsForClient(clientOptions, VerifyClients::NO);
    TlsTest test(kj::mv(clientOptions), kj::mv(serverOptions));

    ErrorNexus e;

    auto pipe = test.io.provider->newTwoWayPipe();

    auto clientPromise = e.wrap(test.tlsClient.wrapClient(kj::mv(pipe.ends[0]), "example.com"));
    auto serverPromise = e.wrap(test.tlsServer.wrapServer(
        kj::AuthenticatedStream { kj::mv(pipe.ends[1]), kj::UnknownPeerIdentity::newInstance() }));

    auto client = clientPromise.wait(test.io.waitScope);
    auto server = serverPromise.wait(test.io.waitScope);

    auto id = server.peerIdentity.downcast<TlsPeerIdentity>();
    KJ_EXPECT(!id->hasCertificate());
  }

  {
    // Non-trusted keys are ignored too (not errors).
    auto clientOptions = TlsTest::defaultClient();
    clientOptions.defaultKeypair = selfSignedKeypair;

    auto serverOptions = makeServerOptionsForClient(clientOptions, VerifyClients::NO);
    TlsTest test(kj::mv(clientOptions), kj::mv(serverOptions));

    ErrorNexus e;

    auto pipe = test.io.provider->newTwoWayPipe();

    auto clientPromise = e.wrap(test.tlsClient.wrapClient(kj::mv(pipe.ends[0]), "example.com"));
    auto serverPromise = e.wrap(test.tlsServer.wrapServer(
        kj::AuthenticatedStream { kj::mv(pipe.ends[1]), kj::UnknownPeerIdentity::newInstance() }));

    auto client = clientPromise.wait(test.io.waitScope);
    auto server = serverPromise.wait(test.io.waitScope);

    auto id = server.peerIdentity.downcast<TlsPeerIdentity>();
    KJ_EXPECT(!id->hasCertificate());
  }
}

class MockConnectionReceiver final: public ConnectionReceiver {
  // This connection receiver allows mocked async connection establishment without the network.

  struct ClientRequest {
    Maybe<Exception> maybeException;
    Own<PromiseFulfiller<Own<AsyncIoStream>>> clientFulfiller;
  };

public:
  MockConnectionReceiver(AsyncIoProvider& provider): provider(provider) {}

  Promise<Own<AsyncIoStream>> accept() override {
    return acceptImpl();
  }

  Promise<AuthenticatedStream> acceptAuthenticated() override {
    return acceptImpl().then([](auto stream) -> AuthenticatedStream {
      return { kj::mv(stream), LocalPeerIdentity::newInstance({}) };
    });
  }

  uint getPort() override {
    return 0;
  }
  void getsockopt(int, int, void*, uint*) override {}
  void setsockopt(int, int, const void*, uint) override {}

  Promise<Own<AsyncIoStream>> connect() {
    // Mock a new successful connection to our receiver.
    return connectImpl();
  }

  Promise<void> badConnect() {
    // Mock a new failed connection to our receiver.
    return connectImpl(KJ_EXCEPTION(DISCONNECTED, "Pipes are leaky")).ignoreResult();
  }

private:
  Promise<Own<AsyncIoStream>> acceptImpl() {
    if (clientRequests.empty()) {
      KJ_ASSERT(!serverFulfiller);
      auto paf = newPromiseAndFulfiller<void>();
      serverFulfiller = kj::mv(paf.fulfiller);
      return paf.promise.then([this] {
        return acceptImpl();
      });
    }

    // This is accepting in FILO order, it shouldn't matter in practice.
    auto request = kj::mv(clientRequests.back());
    clientRequests.removeLast();

    KJ_IF_MAYBE(exception, kj::mv(request.maybeException)) {
      request.clientFulfiller = nullptr;  // The other end had an issue, break the promise.
      return kj::mv(*exception);
    } else {
      auto pipe = provider.newTwoWayPipe();
      request.clientFulfiller->fulfill(kj::mv(pipe.ends[0]));
      return kj::mv(pipe.ends[1]);
    }
  }

  Promise<Own<AsyncIoStream>> connectImpl(Maybe<Exception> maybeException = nullptr) {
    auto paf = newPromiseAndFulfiller<Own<AsyncIoStream>>();
    clientRequests.add(ClientRequest{ kj::mv(maybeException), kj::mv(paf.fulfiller) });

    if (auto fulfiller = kj::mv(serverFulfiller)) {
      fulfiller->fulfill();
    }

    return kj::mv(paf.promise);
  }

  AsyncIoProvider& provider;

  Own<PromiseFulfiller<void>> serverFulfiller;
  Vector<ClientRequest> clientRequests;
};

class TlsReceiverTest final: public TlsTest {
  // TlsReceiverTest augments TlsTest to test TlsConnectionReceiver.
public:
  TlsReceiverTest(): TlsTest() {
    auto baseReceiverPtr = kj::heap<MockConnectionReceiver>(*io.provider);
    baseReceiver = baseReceiverPtr.get();
    receiver = tlsServer.wrapPort(kj::mv(baseReceiverPtr));
  }

  TlsReceiverTest(TlsReceiverTest&&) = delete;
  TlsReceiverTest(const TlsReceiverTest&) = delete;
  TlsReceiverTest& operator=(TlsReceiverTest&&) = delete;
  TlsReceiverTest& operator=(const TlsReceiverTest&) = delete;

  Own<ConnectionReceiver> receiver;
  MockConnectionReceiver* baseReceiver;
};

KJ_TEST("TLS receiver basics") {
  TlsReceiverTest test;

  auto clientPromise = test.baseReceiver->connect().then([&](auto stream) {
    return test.tlsClient.wrapClient(kj::mv(stream), "example.com");
  });
  auto serverPromise = test.receiver->accept();

  auto client = clientPromise.wait(test.io.waitScope);
  auto server = serverPromise.wait(test.io.waitScope);

  test.testConnection(*client, *server);
}

KJ_TEST("TLS receiver experiences pre-TLS error") {
  TlsReceiverTest test;

  KJ_LOG(INFO, "Accepting before a bad connect");
  auto promise = test.receiver->accept();

  KJ_LOG(INFO, "Disappointing our server");
  test.baseReceiver->badConnect();

  // Can't use KJ_EXPECT_THROW_RECOVERABLE_MESSAGE because wait() that returns a value can't throw
  // recoverable exceptions. Can't use KJ_EXPECT_THROW_MESSAGE because non-recoverable exceptions
  // will fork() in -fno-exception which screws up our state.
  promise.then([](auto) {
    KJ_FAIL_EXPECT("expected exception");
  }, [](kj::Exception&& e) {
    KJ_EXPECT(e.getDescription() == "Pipes are leaky");
  }).wait(test.io.waitScope);

  KJ_LOG(INFO, "Trying to load a promise after failure");
  test.receiver->accept().then([](auto) {
    KJ_FAIL_EXPECT("expected exception");
  }, [](kj::Exception&& e) {
    KJ_EXPECT(e.getDescription() == "Pipes are leaky");
  }).wait(test.io.waitScope);
}

KJ_TEST("TLS receiver accepts multiple clients") {
  TlsReceiverTest test;

  auto wrapClient = [&](auto stream) {
    return test.tlsClient.wrapClient(kj::mv(stream), "example.com");
  };

  auto writeToServer = [&](auto client) {
    return test.writeToServer(*client).attach(kj::mv(client));
  };

  auto readFromClient = [&](auto server) {
    return test.readFromClient(*server).attach(kj::mv(server));
  };

  KJ_LOG(INFO, "Requesting a bunch of client connects");
  constexpr auto kClientCount = 20;
  auto clientPromises = Vector<Promise<void>>();
  for (auto i = 0; i < kClientCount; ++i) {
    auto clientPromise = test.baseReceiver->connect().then(wrapClient).then(writeToServer);
    clientPromises.add(kj::mv(clientPromise));
  }

  KJ_LOG(INFO, "Requesting and resolving a bunch of server accepts in sequence");
  for (auto i = 0; i < kClientCount; ++i) {
    // Resolve each receive in sequence like the Supervisor/Network.
    test.receiver->accept().then(readFromClient).wait(test.io.waitScope);
  }

  KJ_LOG(INFO, "Resolving all of our client connects in parallel");
  joinPromises(clientPromises.releaseAsArray()).wait(test.io.waitScope);

  KJ_LOG(INFO, "Requesting one last server accept that we'll never resolve");
  auto extraAcceptPromise = test.receiver->accept().then(readFromClient);
  KJ_EXPECT(!extraAcceptPromise.poll(test.io.waitScope));
}

KJ_TEST("TLS receiver does not stall on client that disconnects before ssl handshake") {
  TlsReceiverTest test;

  auto wrapClient = [&](auto stream) {
    return test.tlsClient.wrapClient(kj::mv(stream), "example.com");
  };

  auto writeToServer = [&](auto client) {
    return test.writeToServer(*client).attach(kj::mv(client));
  };

  auto readFromClient = [&](auto server) {
    return test.readFromClient(*server).attach(kj::mv(server));
  };

  constexpr auto kClientCount = 20;
  auto clientPromises = Vector<Promise<void>>();

  KJ_LOG(INFO, "Requesting the first batch of client connects in parallel");
  for (auto i = 0; i < kClientCount / 2; ++i) {
    auto clientPromise = test.baseReceiver->connect().then(wrapClient).then(writeToServer);
    clientPromises.add(kj::mv(clientPromise));
  }

  KJ_LOG(INFO, "Requesting and resolving a client connect that hangs up before ssl connect");
  KJ_ASSERT(test.baseReceiver->connect().wait(test.io.waitScope));

  KJ_LOG(INFO, "Requesting the second batch of client connects in parallel");
  for (auto i = 0; i < kClientCount / 2; ++i) {
    auto clientPromise = test.baseReceiver->connect().then(wrapClient).then(writeToServer);
    clientPromises.add(kj::mv(clientPromise));
  }

  KJ_LOG(INFO, "Requesting and resolving a bunch of server accepts in sequence");
  for (auto i = 0; i < kClientCount; ++i) {
    test.receiver->accept().then(readFromClient).wait(test.io.waitScope);
  }

  KJ_LOG(INFO, "Resolving all of our client connects in parallel");
  joinPromises(clientPromises.releaseAsArray()).wait(test.io.waitScope);

  KJ_LOG(INFO, "Requesting one last server accept that we'll never resolve");
  auto extraAcceptPromise = test.receiver->accept().then(readFromClient);
  KJ_EXPECT(!extraAcceptPromise.poll(test.io.waitScope));
}

KJ_TEST("TLS receiver does not stall on hung client") {
  TlsReceiverTest test;

  auto wrapClient = [&](auto stream) {
    return test.tlsClient.wrapClient(kj::mv(stream), "example.com");
  };

  auto writeToServer = [&](auto client) {
    return test.writeToServer(*client).attach(kj::mv(client));
  };

  auto readFromClient = [&](auto server) {
    return test.readFromClient(*server).attach(kj::mv(server));
  };

  constexpr auto kClientCount = 20;
  auto clientPromises = Vector<Promise<void>>();

  KJ_LOG(INFO, "Requesting the first batch of client connects in parallel");
  for (auto i = 0; i < kClientCount / 2; ++i) {
    auto clientPromise = test.baseReceiver->connect().then(wrapClient).then(writeToServer);
    clientPromises.add(kj::mv(clientPromise));
  }

  KJ_LOG(INFO, "Requesting and resolving a client connect that never does ssl connect");
  auto hungClient = test.baseReceiver->connect().wait(test.io.waitScope);
  KJ_ASSERT(hungClient);

  KJ_LOG(INFO, "Requesting the second batch of client connects in parallel");
  for (auto i = 0; i < kClientCount / 2; ++i) {
    auto clientPromise = test.baseReceiver->connect().then(wrapClient).then(writeToServer);
    clientPromises.add(kj::mv(clientPromise));
  }

  KJ_LOG(INFO, "Requesting and resolving a bunch of server accepts in sequence");
  for (auto i = 0; i < kClientCount; ++i) {
    test.receiver->accept().then(readFromClient).wait(test.io.waitScope);
  }

  KJ_LOG(INFO, "Resolving all of our client connects in parallel");
  joinPromises(clientPromises.releaseAsArray()).wait(test.io.waitScope);

  KJ_LOG(INFO, "Releasing the hung client");
  hungClient = {};

  KJ_LOG(INFO, "Requesting one last server accept that we'll never resolve");
  auto extraAcceptPromise = test.receiver->accept().then(readFromClient);
  KJ_EXPECT(!extraAcceptPromise.poll(test.io.waitScope));
}

kj::Promise<void> expectRead(kj::AsyncInputStream& in, kj::StringPtr expected) {
  if (expected.size() == 0) return kj::READY_NOW;

  auto buffer = kj::heapArray<char>(expected.size());

  auto promise = in.tryRead(buffer.begin(), 1, buffer.size());
  return promise.then([&in,expected,buffer=kj::mv(buffer)](size_t amount) {
    if (amount == 0) {
      KJ_FAIL_ASSERT("expected data never sent", expected);
    }

    auto actual = buffer.slice(0, amount);
    if (memcmp(actual.begin(), expected.begin(), actual.size()) != 0) {
      KJ_FAIL_ASSERT("data from stream doesn't match expected", expected, actual);
    }

    return expectRead(in, expected.slice(amount));
  });
}

kj::Promise<void> expectEnd(kj::AsyncInputStream& in) {
  static char buffer;

  auto promise = in.tryRead(&buffer, 1, 1);
  return promise.then([](size_t amount) {
    KJ_ASSERT(amount == 0, "expected EOF");
  });
}

KJ_TEST("NetworkHttpClient connect with tlsStarter") {
  auto io = kj::setupAsyncIo();
  auto listener1 = io.provider->getNetwork().parseAddress("127.0.0.1", 0)
      .wait(io.waitScope)->listen();

  auto acceptLoop KJ_UNUSED = listener1->accept().then([](Own<kj::AsyncIoStream> stream) {
    return stream->pumpTo(*stream).attach(kj::mv(stream)).ignoreResult();
  }).eagerlyEvaluate(nullptr);

  HttpClientSettings clientSettings;
  kj::TimerImpl clientTimer(kj::origin<kj::TimePoint>());
  HttpHeaderTable headerTable;
  TlsContext tls;

  auto tlsNetwork = tls.wrapNetwork(io.provider->getNetwork());
  clientSettings.tlsContext = tls;
  auto client = newHttpClient(clientTimer, headerTable,
      io.provider->getNetwork(), *tlsNetwork, clientSettings);
  kj::HttpConnectSettings httpConnectSettings = { false, nullptr };
  kj::TlsStarterCallback tlsStarter;
  httpConnectSettings.tlsStarter = tlsStarter;
  auto request = client->connect(
      kj::str("127.0.0.1:", listener1->getPort()), HttpHeaders(headerTable), httpConnectSettings);

  KJ_ASSERT(tlsStarter != nullptr);

  auto buf = kj::heapArray<char>(4);

  auto promises = kj::heapArrayBuilder<kj::Promise<void>>(2);
  promises.add(request.connection->write("hello", 5));
  promises.add(expectRead(*request.connection, "hello"_kj));
  kj::joinPromisesFailFast(promises.finish())
      .then([io=kj::mv(request.connection)]() mutable {
    io->shutdownWrite();
    return expectEnd(*io).attach(kj::mv(io));
  }).attach(kj::mv(listener1)).wait(io.waitScope);
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
