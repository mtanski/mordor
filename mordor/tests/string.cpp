// Copyright (c) 2010 - Mozy, Inc.

#include "mordor/string.h"
#include "mordor/test/test.h"

using namespace Mordor;

MORDOR_UNITTEST(String, dataFromHexstring)
{
    MORDOR_TEST_ASSERT_EQUAL(dataFromHexstring(""), "");
    MORDOR_TEST_ASSERT_EQUAL(dataFromHexstring("00"), std::string("\0", 1));
    MORDOR_TEST_ASSERT_EQUAL(dataFromHexstring("abcd"), "\xab\xcd");
    MORDOR_TEST_ASSERT_EQUAL(dataFromHexstring("01eF"), "\x01\xef");
    MORDOR_TEST_ASSERT_EXCEPTION(dataFromHexstring("0"),
        std::invalid_argument);
    MORDOR_TEST_ASSERT_EXCEPTION(dataFromHexstring("fg"),
        std::invalid_argument);
    MORDOR_TEST_ASSERT_EXCEPTION(dataFromHexstring("fG"),
        std::invalid_argument);
    MORDOR_TEST_ASSERT_EXCEPTION(dataFromHexstring(std::string("\0\0", 2)),
        std::invalid_argument);
}

MORDOR_UNITTEST(String, base64decode)
{
    MORDOR_TEST_ASSERT_EQUAL(base64decode("H+LksF7FISl/Sw=="),
        "\x1f\xe2\xe4\xb0^\xc5!)\x7fK");
    MORDOR_TEST_ASSERT_EQUAL(base64decode("H-LksF7FISl_Sw==", "-_"),
        "\x1f\xe2\xe4\xb0^\xc5!)\x7fK");
    MORDOR_TEST_ASSERT_EQUAL(urlsafeBase64decode("H-LksF7FISl_Sw=="),
        "\x1f\xe2\xe4\xb0^\xc5!)\x7fK");
}

MORDOR_UNITTEST(String, base64encode)
{
    MORDOR_TEST_ASSERT_EQUAL(base64encode("\x1f\xe2\xe4\xb0^\xc5!)\x7fK"),
        "H+LksF7FISl/Sw==");
    MORDOR_TEST_ASSERT_EQUAL(base64encode("\x1f\xe2\xe4\xb0^\xc5!)\x7fK",
        "-_"), "H-LksF7FISl_Sw==");
    MORDOR_TEST_ASSERT_EQUAL(urlsafeBase64encode(
        "\x1f\xe2\xe4\xb0^\xc5!)\x7fK"), "H-LksF7FISl_Sw==");
}

MORDOR_UNITTEST(String, sha0sum)
{
    MORDOR_TEST_ASSERT_EQUAL(hexstringFromData(sha0sum("")), "f96cea198ad1dd5617ac084a3d92c6107708c0ef");
    MORDOR_TEST_ASSERT_EQUAL(hexstringFromData(sha0sum("1234567890")), "786abc00fc4c0ab7ea5f0f2bd85fb9ab00c2ad82");
    MORDOR_TEST_ASSERT_EQUAL(hexstringFromData(sha0sum((const void *)"\x7e\x54\xe4\xbc\x27\x00\x40\xab", 8)), "ea1d7982eb4c6201498ece16539ce174735b6a21");
}
