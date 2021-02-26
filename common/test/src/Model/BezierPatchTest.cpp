/*
 Copyright (C) 2010-2017 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Model/BezierPatch.h"

#include <vecmath/vec.h>
#include <vecmath/vec_io.h>

#include <tuple>
#include <vector>

#include "Catch2.h"

namespace TrenchBroom {
    namespace Model {
        TEST_CASE("BezierPatch.evaluate") {
            using P = BezierPatch::Point;
            using T = std::tuple<size_t, size_t, std::vector<P>, size_t, std::vector<P>>;

            const auto [  w,  h, controlPoints,                          subdiv, expectedGrid ] = GENERATE(values<T>({
                       {  3,  3, { P{0, 0, 0}, P{1, 0, 1}, P{2, 0, 0},
                                   P{0, 1, 1}, P{1, 1, 2}, P{2, 1, 1},
                                   P{0, 2, 0}, P{1, 2, 1}, P{2, 2, 0} }, 2,      { P{0, 0,   0},     P{0.5, 0,   0.375}, P{1, 0,   0.5},   P{1.5, 0,   0.375}, P{2, 0,   0}, 
                                                                                   P{0, 0.5, 0.375}, P{0.5, 0.5, 0.75},  P{1, 0.5, 0.875}, P{1.5, 0.5, 0.75},  P{2, 0.5, 0.375}, 
                                                                                   P{0, 1,   0.5},   P{0.5, 1,   0.875}, P{1, 1,   1},     P{1.5, 1,   0.875}, P{2, 1,   0.5}, 
                                                                                   P{0, 1.5, 0.375}, P{0.5, 1.5, 0.75},  P{1, 1.5, 0.875}, P{1.5, 1.5, 0.75},  P{2, 1.5, 0.375}, 
                                                                                   P{0, 2,   0},     P{0.5, 2,   0.375}, P{1, 2,   0.5},   P{1.5, 2,   0.375}, P{2, 2,   0} } }
            }));

            const auto patch = BezierPatch(w, h, controlPoints, "");
            CHECK(patch.evaluate(subdiv) == expectedGrid);
        }
    }
}
