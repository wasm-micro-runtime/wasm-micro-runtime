# Copyright (c) 2021, RT-Thread Development Team
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
import os
from building import *

cwd = GetCurrentDir()
objs = []
if GetDepend('PKG_USING_WAMR'):
    objs += SConscript(os.path.join(cwd, 'build-scripts', 'SConscript'))
    objs += SConscript(os.path.join(cwd, 'product-mini', 'platforms',
                                    'rt-thread', 'SConscript'))
Return('objs')
