/*
 Copyright (C) 2021 Kristian Duske

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

#pragma once

#include "Color.h"
#include "Renderer/Renderable.h"
#include "Renderer/TexturedIndexArrayRenderer.h"

#include <vector>

namespace TrenchBroom {
    namespace Model {
        class PatchNode;
    }

    namespace Renderer {
        class RenderBatch;
        class RenderContext;
        class VboManager;

        class PatchRenderer : public IndexedRenderable {
        private:
            bool m_valid = true;
            std::vector<Model::PatchNode*> m_patchNodes;

            TexturedIndexArrayRenderer m_indexArrayRenderer;
            Color m_defaultColor;
            bool m_grayscale;
            bool m_tint;
            Color m_tintColor;
            float m_alpha;
        public:
            PatchRenderer();

            void setDefaultColor(const Color& faceColor);
            void setGrayscale(bool grayscale);
            void setTint(bool tint);
            void setTintColor(const Color& color);
            void setTransparencyAlpha(float alpha);

            void setPatches(std::vector<Model::PatchNode*> patchNodes);
            void invalidate();
            void clear();

            void render(RenderContext& renderContext, RenderBatch& renderBatch);
        private:
            void validate();
        private: // implement IndexedRenderable interface
            void prepareVerticesAndIndices(VboManager& vboManager) override;
            void doRender(RenderContext& renderContext) override;
        };
    }
}
