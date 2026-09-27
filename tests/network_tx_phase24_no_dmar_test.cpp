#include <assert.h>

#include "kernel/shell.h"

using namespace kernel;

int main()
{
    assert(shell::nicinfo_mode_from_args("tx", "iommu", nullptr) ==
           shell::NICINFO_MODE_TX_IOMMU);
    assert(shell::NICINFO_TX_IOMMU_EXPECTED_LINES <=
           shell::NICINFO_TX_IOMMU_MAX_LINES);
    return 0;
}
