#
# Minimal exit module Python wrapper for GVSoC
#
import gvsoc.systree
import os

_MODEL_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'model')

class ExitModule(gvsoc.systree.Component):
    """Write any value to this peripheral to stop the simulation.
    Write 0 for success, non-zero for failure exit code."""

    def __init__(self, parent, name):
        super().__init__(parent, name)
        self.add_sources([os.path.join(_MODEL_DIR, 'exit_module.cpp')])

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')
