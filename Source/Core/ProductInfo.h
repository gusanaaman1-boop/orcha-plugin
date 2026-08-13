// Product-wide identifiers. Nothing else in the codebase should hard-code the
// product name, maker or company - same convention as FOUR COLOR and the rest
// of the line, so the strings a host lists stay consistent.

#pragma once

namespace orcha::productInfo
{
    inline constexpr const char* name    = "ORCHA";

    //  The company is what a host lists under the manufacturer field; the
    //  maker is the person, and the two are not written the same way.
    inline constexpr const char* company = "Naaman";
    inline constexpr const char* maker   = "GUSSA NAAMAN";
}
