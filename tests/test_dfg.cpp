#include "Check.h"
#include "Dfg.h"

#include <math.h>
#include <stdio.h>

int main()
{
    using namespace zenapp::ibl;

    CHECK(floatToHalf(0.0f) == 0x0000);
    CHECK(floatToHalf(1.0f) == 0x3C00);
    CHECK(floatToHalf(0.5f) == 0x3800);
    CHECK(floatToHalf(-2.0f) == 0xC000);
    CHECK(floatToHalf(65504.0f) == 0x7BFF);
    CHECK(floatToHalf(100000.0f) == 0x7C00);
    CHECK(floatToHalf(6.103515625e-05f) == 0x0400);
    CHECK(floatToHalf(5.960464477539063e-08f) == 0x0001);
    CHECK(floatToHalf(1e-10f) == 0x0000);

    float u[2];
    hammersley(1, 0.25f, u);
    CHECK(u[0] == 0.25f && u[1] == 0.5f);
    hammersley(2, 0.25f, u);
    CHECK(u[0] == 0.5f && u[1] == 0.25f);
    hammersley(3, 0.25f, u);
    CHECK(u[0] == 0.75f && u[1] == 0.75f);

    const unsigned size = 32;
    ct::Vector<float> rg;
    computeDfg(size, 512, &rg);
    CHECK(rg.size() == static_cast<size_t>(size) * size * 2);
    bool sane = true;
    for (size_t i = 0; i < rg.size(); ++i)
        sane = sane && isfinite(rg[i]) && rg[i] >= 0.0f && rg[i] <= 1.05f;
    CHECK(sane);

    const auto y = [&](unsigned row, unsigned column) {
        return rg[(static_cast<size_t>(row) * size + column) * 2 + 1];
    };
    const auto x = [&](unsigned row, unsigned column) {
        return rg[(static_cast<size_t>(row) * size + column) * 2];
    };
    CHECK(y(0, size - 1) > 0.95f);
    CHECK(x(0, size - 1) < 0.1f);
    CHECK(y(size - 1, size / 2) < y(0, size / 2));
    CHECK(y(size - 1, size - 1) < y(0, size - 1));
    CHECK(x(0, 0) > x(0, size - 1));

    printf(failures ? "test_dfg: %d failures\n" : "test_dfg: all passed\n", failures);
    return failures ? 1 : 0;
}
