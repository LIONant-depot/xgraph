# xgraph

The mechanics that LIONant's graph editors share - the Material Editor, the Render Graph Editor, later visual scripting: a layered
layout (columns by dependency depth, wires through slots in the columns they cross, crossing sweeps), an orthogonal wire router
(lanes per gutter, crossings at right angles, junction dots), and the order functions of a stack of steps (topological order, the
positions a step may move to). Header only, no ImGui, no domain types.

    source/xgraph_layout.h      layout
    source/xgraph_routes.h      routes (includes the layout)
    source/xgraph_order.h       order functions

Self-checks (from this folder):

    cl /nologo /std:c++latest /EHsc /O2 /Zc:preprocessor /Isource test/xgraph_test.cpp
    xgraph_test [fuzz cases]
