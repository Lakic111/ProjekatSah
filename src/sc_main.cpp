#include "vp.hpp"
#include "tb.hpp"

int sc_main(int argc, char* argv[]) {
    vp uut("uut");
    tb_vp tb("tb");

    tb.isoc.bind(uut.s_cpu);

    // Poveži "interrupt" linije (done događaje) sa testbench-om
    tb.connect_irq(&uut.nccDone(), &uut.dmaDone());

    sc_core::sc_start();

    return 0;
}
