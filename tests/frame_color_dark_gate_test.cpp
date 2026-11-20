#include <base/DominantDarkSceneGate.h>

#include <cassert>
#include <iostream>

using Gate = hyperhdr::DominantDarkSceneGate;
using Decision = Gate::Decision;

int main()
{
    // Brief dark frames must not shut off the room (e.g. movie cuts).
    Gate cuts;
    assert(cuts.update(95.0, 0.65, 0) == Decision::Normal);
    assert(cuts.update(1.7, 0.019, 100) == Decision::HoldPrevious);
    assert(cuts.update(100.0, 0.75, 190) == Decision::Normal);
    assert(cuts.update(1.7, 0.019, 220) == Decision::HoldPrevious);
    assert(cuts.update(1.7, 0.019, 600) == Decision::Dark);

    // A short overlay must not wake a dark room.
    assert(cuts.update(1.7, 0.019, 633) == Decision::Dark);
    assert(cuts.update(22.0, 0.11, 667) == Decision::Dark);
    assert(cuts.update(1.7, 0.019, 700) == Decision::Dark);

    // An unmistakable full-frame scene resumes immediately.
    assert(cuts.update(90.0, 0.55, 733) == Decision::Resumed);
    assert(cuts.update(90.0, 0.55, 766) == Decision::Normal);

    // Mild but genuine scene light requires stable evidence before waking.
    Gate dim;
    assert(dim.update(0.0, 0.0, 1000) == Decision::HoldPrevious);
    assert(dim.update(0.0, 0.0, 1370) == Decision::Dark);
    assert(dim.update(20.0, 0.12, 1400) == Decision::Dark);
    assert(dim.update(20.0, 0.12, 1600) == Decision::Dark);
    assert(dim.update(20.0, 0.12, 1640) == Decision::Resumed);

    // Fluctuations at the threshold cannot repeatedly trigger on/off.
    Gate noise;
    assert(noise.update(9.0, 0.03, 0) == Decision::HoldPrevious);
    assert(noise.update(13.0, 0.09, 60) == Decision::Normal);
    assert(noise.update(9.0, 0.03, 120) == Decision::HoldPrevious);
    assert(noise.update(13.0, 0.09, 180) == Decision::Normal);
    assert(noise.update(9.0, 0.03, 240) == Decision::HoldPrevious);
    assert(noise.update(13.0, 0.09, 300) == Decision::Normal);

    // A completely black image starts dark rather than forcing nonzero RGB.
    Gate fresh;
    assert(fresh.update(0, 0, 0) == Decision::HoldPrevious);
    assert(fresh.update(0, 0, 350) == Decision::Dark);

    std::cout << "DominantDarkSceneGate: all tests passed\n";
}
