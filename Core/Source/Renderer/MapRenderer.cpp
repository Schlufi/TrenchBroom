/*
 Copyright (C) 2010-2012 Kristian Duske

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
 along with TrenchBroom.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "MapRenderer.h"
#include <set>
#include <algorithm>
#include <cassert>

#include "Model/Map/Brush.h"
#include "Model/Map/BrushGeometry.h"
#include "Model/Map/Entity.h"
#include "Model/Map/EntityDefinition.h"
#include "Model/Map/Face.h"
#include "Model/Map/Map.h"
#include "Model/Preferences.h"
#include "Model/Selection.h"
#include "Controller/Camera.h"
#include "Controller/Editor.h"
#include "Controller/Options.h"
#include "Renderer/EntityRendererManager.h"
#include "Renderer/EntityRenderer.h"
#include "Renderer/EntityClassnameAnchor.h"
#include "Renderer/Figures/Figure.h"
#include "Renderer/FontManager.h"
#include "Renderer/GridRenderer.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderUtils.h"
#include "Renderer/TextRenderer.h"
#include "Renderer/Vbo.h"
#include "Utilities/Filter.h"

namespace TrenchBroom {
    namespace Renderer {
        static const int VertexSize = 3 * sizeof(float);
        static const int ColorSize = 4;
        static const int TexCoordSize = 2 * sizeof(float);

        void MapRenderer::addEntities(const vector<Model::Entity*>& entities) {
            m_changeSet.entitiesAdded(entities);

            for (unsigned int i = 0; i < entities.size(); i++)
                addBrushes(entities[i]->brushes());
        }

        void MapRenderer::removeEntities(const vector<Model::Entity*>& entities) {
            m_changeSet.entitiesRemoved(entities);

            for (unsigned int i = 0; i < entities.size(); i++)
                removeBrushes(entities[i]->brushes());
        }

        void MapRenderer::addBrushes(const vector<Model::Brush*>& brushes) {
            m_changeSet.brushesAdded(brushes);
        }

        void MapRenderer::removeBrushes(const vector<Model::Brush*>& brushes) {
            m_changeSet.brushesRemoved(brushes);
        }

        void MapRenderer::entitiesWereAdded(const vector<Model::Entity*>& entities) {
            addEntities(entities);
        }

        void MapRenderer::entitiesWillBeRemoved(const vector<Model::Entity*>& entities) {
            removeEntities(entities);
        }

        void MapRenderer::propertiesDidChange(const vector<Model::Entity*>& entities) {
            m_changeSet.entitiesChanged(entities);

            Model::Entity* worldspawn = m_editor.map().worldspawn(true);
            if (find(entities.begin(), entities.end(), worldspawn) != entities.end()) {
                // if mods changed, invalidate renderer cache here
            }
        }

        void MapRenderer::brushesWereAdded(const vector<Model::Brush*>& brushes) {
            addBrushes(brushes);
        }

        void MapRenderer::brushesWillBeRemoved(const vector<Model::Brush*>& brushes) {
            removeBrushes(brushes);
        }

        void MapRenderer::brushesDidChange(const vector<Model::Brush*>& brushes) {
            m_changeSet.brushesChanged(brushes);

            // TODO use a tree or a hash set here to optimize
            vector<Model::Entity*> entities;
            for (unsigned int i = 0; i < brushes.size(); i++) {
                Model::Entity* entity = brushes[i]->entity();
                if (!entity->worldspawn()) {
                    if (find(entities.begin(), entities.end(), entity) == entities.end())
                        entities.push_back(entity);
                }
            }

            m_changeSet.entitiesChanged(entities);
        }

        void MapRenderer::facesDidChange(const vector<Model::Face*>& faces) {
            m_changeSet.facesChanged(faces);
        }

        void MapRenderer::mapLoaded(Model::Map& map) {
            addEntities(map.entities());
        }

        void MapRenderer::mapCleared(Model::Map& map) {
        }

        void MapRenderer::selectionAdded(const Model::SelectionEventData& event) {
            if (!event.entities.empty())
                m_changeSet.entitiesSelected(event.entities);
            if (!event.brushes.empty())
                m_changeSet.brushesSelected(event.brushes);
            if (!event.faces.empty())
                m_changeSet.facesSelected(event.faces);
        }

        void MapRenderer::selectionRemoved(const Model::SelectionEventData& event) {
            if (!event.entities.empty())
                m_changeSet.entitiesDeselected(event.entities);
            if (!event.brushes.empty())
                m_changeSet.brushesDeselected(event.brushes);
            if (!event.faces.empty())
                m_changeSet.facesDeselected(event.faces);
        }

        void MapRenderer::writeFaceVertices(RenderContext& context, Model::Face& face, VboBlock& block) {
            Vec2f texCoords, gridCoords;

            Model::Assets::Texture* texture = face.texture();
            const Vec4f& faceColor = texture != NULL && !texture->dummy ? texture->averageColor : context.preferences.faceColor();
            const Vec4f& edgeColor = context.preferences.edgeColor();
            unsigned int width = texture != NULL ? texture->width : 1;
            unsigned int height = texture != NULL ? texture->height : 1;

            unsigned int offset = 0;
            const vector<Model::Vertex*>& vertices = face.vertices();
            for (unsigned int i = 0; i < vertices.size(); i++) {
                const Model::Vertex* vertex = vertices[i];
                gridCoords = face.gridCoords(vertex->position);
                texCoords = face.textureCoords(vertex->position);
                texCoords.x /= width;
                texCoords.y /= height;

                offset = block.writeVec(gridCoords, offset);
                offset = block.writeVec(texCoords, offset);
                offset = block.writeColor(edgeColor, offset);
                offset = block.writeColor(faceColor, offset);
                offset = block.writeVec(vertex->position, offset);
            }
        }

        unsigned int MapRenderer::writeFaceIndices(RenderContext& context, Model::Face& face, VboBlock& block, unsigned int offset) {
            unsigned int baseIndex = face.vboBlock()->address / (TexCoordSize + TexCoordSize + ColorSize + ColorSize + VertexSize);
            unsigned int vertexCount = static_cast<unsigned int>(face.vertices().size());

            for (unsigned int i = 1; i < vertexCount - 1; i++) {
                offset = block.writeUInt32(baseIndex, offset);
                offset = block.writeUInt32(baseIndex + i, offset);
                offset = block.writeUInt32(baseIndex + i + 1, offset);
            }
            
            return offset;
        }

        unsigned int MapRenderer::writeEdgeIndices(RenderContext& context, Model::Face& face, VboBlock& block, unsigned int offset) {
            unsigned int baseIndex = face.vboBlock()->address / (TexCoordSize + TexCoordSize + ColorSize + ColorSize + VertexSize);
            unsigned int vertexCount = static_cast<unsigned int>(face.vertices().size());
            
            for (unsigned int i = 0; i < vertexCount - 1; i++) {
                offset = block.writeUInt32(baseIndex + i, offset);
                offset = block.writeUInt32(baseIndex + i + 1, offset);
            }
            
            offset = block.writeUInt32(baseIndex + vertexCount - 1, offset);
            offset = block.writeUInt32(baseIndex, offset);
            
            return offset;
        }

        void MapRenderer::writeEntityBounds(RenderContext& context, Model::Entity& entity, VboBlock& block) {
            Vec3f t;
            const BBox& bounds = entity.bounds();
            const Model::EntityDefinitionPtr definition = entity.entityDefinition();
            Vec4f color = definition != NULL ? definition->color : context.preferences.entityBoundsColor();
            color.w = context.preferences.entityBoundsColor().w;

            unsigned int offset = 0;

            t = bounds.min;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.x = bounds.max.x;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.x = bounds.min.x;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.y = bounds.max.y;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.y = bounds.min.y;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.z = bounds.max.z;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t = bounds.max;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.x = bounds.min.x;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.x = bounds.max.x;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.y = bounds.min.y;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.y = bounds.max.y;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.z = bounds.min.z;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t = bounds.min;
            t.x = bounds.max.x;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.y = bounds.max.y;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.y = bounds.min.y;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.z = bounds.max.z;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t = bounds.min;
            t.y = bounds.max.y;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.x = bounds.max.x;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.x = bounds.min.x;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.z = bounds.max.z;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t = bounds.min;
            t.z = bounds.max.z;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.x = bounds.max.x;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.x = bounds.min.x;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);

            t.y = bounds.max.y;
            offset = block.writeColor(color, offset);
            offset = block.writeVec(t, offset);
        }

        void MapRenderer::rebuildFaceIndexBuffers(RenderContext& context) {
            for (FaceIndexBlocks::iterator it = m_faceIndexBlocks.begin(); it != m_faceIndexBlocks.end(); ++it)
                it->second->freeBlock();
            m_faceIndexBlocks.clear();
            
            if (m_edgeIndexBlock != NULL) {
                m_edgeIndexBlock->freeBlock();
                m_edgeIndexBlock = NULL;
            }

            typedef vector<Face*> Faces;
            typedef pair<Faces, unsigned int> FaceCountEntry;
            typedef map<Model::Assets::Texture*, FaceCountEntry> TextureFaces;
            TextureFaces textureFaces;
            Faces allFaces;
            unsigned int edgeBlockSize = 0;
            
            // determine the sizes for the VBO blocks and add them to the TextureFaces map
            const vector<Model::Entity*>& entities = m_editor.map().entities();
            for (unsigned int i = 0; i < entities.size(); i++) {
                if (context.filter.entityVisible(*entities[i])) {
                    const vector<Model::Brush*>& brushes = entities[i]->brushes();
                    for (unsigned int j = 0; j < brushes.size(); j++) {
                        if (context.filter.brushVisible(*brushes[j])) {
                            Model::Brush* brush = brushes[j];
                            if (!brush->selected()) {
                                const vector<Model::Face*>& faces = brush->faces();
                                for (unsigned int k = 0; k < faces.size(); k++) {
                                    Model::Face* face = faces[k];
                                    if (!face->selected()) {
                                        Model::Assets::Texture* texture = face->texture();
                                        TextureFaces::iterator it = textureFaces.find(texture);
                                        if (it == textureFaces.end()) {
                                            textureFaces[texture].first.push_back(face);
                                            textureFaces[texture].second = (face->vertices().size() - 2) * 3;
                                        } else {
                                            it->second.first.push_back(face);
                                            it->second.second += (face->vertices().size() - 2) * 3; 
                                        }
                                        allFaces.push_back(face);
                                        edgeBlockSize += face->vertices().size() * 2;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (allFaces.empty())
                return;
            
            // write the face blocks
            m_faceIndexVbo->activate();
            m_faceIndexVbo->map();
            for (TextureFaces::iterator it = textureFaces.begin(); it != textureFaces.end(); ++it) {
                Model::Assets::Texture* texture = it->first;
                FaceCountEntry& entry = it->second;
                Faces& faces = entry.first;
                unsigned int size = entry.second;
                
                unsigned int offset = 0;
                VboBlock& block = m_faceIndexVbo->allocBlock(size * sizeof(unsigned int));
                for (int i = 0; i < faces.size(); i++)
                    offset = writeFaceIndices(context, *faces[i], block, offset);
                m_faceIndexBlocks[texture] = &block;
            }
            m_faceIndexVbo->unmap();
            m_faceIndexVbo->deactivate();
            
            m_edgeIndexVbo->activate();
            m_edgeIndexVbo->map();
            unsigned int offset = 0;
            m_edgeIndexBlock = &m_edgeIndexVbo->allocBlock(edgeBlockSize * sizeof(unsigned int));
            for (unsigned int i = 0; i < allFaces.size(); i++)
                offset = writeEdgeIndices(context, *allFaces[i], *m_edgeIndexBlock, offset);
            m_edgeIndexVbo->unmap();
            m_edgeIndexVbo->deactivate();
        }

        void MapRenderer::rebuildSelectedFaceIndexBuffers(RenderContext& context) {
            for (FaceIndexBlocks::iterator it = m_selectedFaceIndexBlocks.begin(); it != m_selectedFaceIndexBlocks.end(); ++it)
                it->second->freeBlock();
            m_selectedFaceIndexBlocks.clear();
            
            if (m_selectedEdgeIndexBlock != NULL) {
                m_selectedEdgeIndexBlock->freeBlock();
                m_selectedEdgeIndexBlock = NULL;
            }
            
            Model::Selection& selection = m_editor.map().selection();
            if (selection.faces().empty() && selection.brushes().empty())
                return;
            
            typedef vector<Face*> Faces;
            typedef pair<Faces, unsigned int> FaceCountEntry;
            typedef map<Model::Assets::Texture*, FaceCountEntry> TextureFaces;
            TextureFaces textureFaces;
            Faces allFaces;
            unsigned int edgeBlockSize = 0;
            
            // collect all selected faces in allFaces
            const vector<Model::Brush*>& selectedBrushes = selection.brushes();
            for (unsigned int i = 0; i < selectedBrushes.size(); i++) {
                const vector<Model::Face*>& faces = selectedBrushes[i]->faces();
                allFaces.insert(allFaces.end(), faces.begin(), faces.end());
            }
            
            const vector<Model::Face*>& selectedFaces = selection.faces();
            allFaces.insert(allFaces.end(), selectedFaces.begin(), selectedFaces.end());
            
            // sort them into the texture face map
            for (int i = 0; i < allFaces.size(); i++) {
                Model::Face* face = allFaces[i];
                Model::Assets::Texture* texture = face->texture();
                TextureFaces::iterator it = textureFaces.find(texture);
                if (it == textureFaces.end()) {
                    textureFaces[texture].first.push_back(face);
                    textureFaces[texture].second = (face->vertices().size() - 2) * 3;
                } else {
                    it->second.first.push_back(face);
                    it->second.second += (face->vertices().size() - 2) * 3; 
                }
                edgeBlockSize += face->vertices().size() * 2;
            }
            
            // write the face blocks
            m_faceIndexVbo->activate();
            m_faceIndexVbo->map();
            for (TextureFaces::iterator it = textureFaces.begin(); it != textureFaces.end(); ++it) {
                Model::Assets::Texture* texture = it->first;
                FaceCountEntry& entry = it->second;
                Faces& faces = entry.first;
                unsigned int size = entry.second;
                
                unsigned int offset = 0;
                VboBlock& block = m_faceIndexVbo->allocBlock(size * sizeof(unsigned int));
                for (int i = 0; i < faces.size(); i++)
                    offset = writeFaceIndices(context, *faces[i], block, offset);
                m_selectedFaceIndexBlocks[texture] = &block;
            }
            m_faceIndexVbo->unmap();
            m_faceIndexVbo->deactivate();
            
            m_edgeIndexVbo->activate();
            m_edgeIndexVbo->map();
            unsigned int offset = 0;
            m_selectedEdgeIndexBlock = &m_edgeIndexVbo->allocBlock(edgeBlockSize * sizeof(unsigned int));
            for (unsigned int i = 0; i < allFaces.size(); i++)
                offset = writeEdgeIndices(context, *allFaces[i], *m_selectedEdgeIndexBlock, offset);
            m_edgeIndexVbo->unmap();
            m_edgeIndexVbo->deactivate();
        }

        void MapRenderer::validateEntityRendererCache(RenderContext& context) {
            if (!m_entityRendererCacheValid) {
                const vector<string>& mods = m_editor.map().mods();
                EntityRenderers::iterator it;
                for (it = m_entityRenderers.begin(); it != m_entityRenderers.end(); ++it) {
                    EntityRenderer* renderer = m_entityRendererManager->entityRenderer(*it->first, mods);
                    if (renderer != NULL) m_entityRenderers[it->first] = renderer;
                    else m_entityRenderers.erase(it);
                }
                for (it = m_selectedEntityRenderers.begin(); it != m_selectedEntityRenderers.end(); ++it) {
                    EntityRenderer* renderer = m_entityRendererManager->entityRenderer(*it->first, mods);
                    if (renderer != NULL) m_selectedEntityRenderers[it->first] = renderer;
                    else m_selectedEntityRenderers.erase(it);
                }
                m_entityRendererCacheValid = true;
            }
        }

        void MapRenderer::validateAddedEntities(RenderContext& context) {
            const vector<Model::Entity*>& addedEntities = m_changeSet.addedEntities();
            if (!addedEntities.empty()) {
                const string& fontName = context.preferences.rendererFontName();
				int fontSize = context.preferences.rendererFontSize();
                FontDescriptor descriptor(fontName, fontSize);

                m_entityBoundsVbo->activate();
                m_entityBoundsVbo->map();

                for (unsigned int i = 0; i < addedEntities.size(); i++) {
                    Model::Entity* entity = addedEntities[i];
                    if (context.filter.entityVisible(*entity)) {
                        VboBlock& block = m_entityBoundsVbo->allocBlock(6 * 4 * (ColorSize + VertexSize));
                        writeEntityBounds(context, *entity, block);
                        entity->setVboBlock(&block);
                        m_entityBoundsVertexCount += 6 * 4;

                        EntityRenderer* renderer = m_entityRendererManager->entityRenderer(*entity, m_editor.map().mods());
                        if (renderer != NULL)
                            m_entityRenderers[entity] = renderer;

                        const string& classname = *entity->classname();
                        EntityClassnameAnchor* anchor = new EntityClassnameAnchor(*entity);
                        AnchorPtr anchorPtr(anchor);
                        m_classnameRenderer->addString(entity->uniqueId(), classname, descriptor, anchorPtr);
                    }
                }

                m_entityBoundsVbo->unmap();
                m_entityBoundsVbo->deactivate();
            }
        }

        void MapRenderer::validateRemovedEntities(RenderContext& context) {
            const vector<Model::Entity*>& removedEntities = m_changeSet.removedEntities();
            if (!removedEntities.empty()) {
                m_entityBoundsVbo->activate();
                m_entityBoundsVbo->map();

                for (unsigned int i = 0; i < removedEntities.size(); i++) {
                    Model::Entity* entity = removedEntities[i];
                    if (context.filter.entityVisible(*entity)) {
                        entity->setVboBlock(NULL);
                        m_entityRenderers.erase(entity);
                        m_classnameRenderer->removeString(entity->uniqueId());
                    }
                }

                m_entityBoundsVertexCount -= 6 * 4 * removedEntities.size();
                m_entityBoundsVbo->pack();
                m_entityBoundsVbo->unmap();
                m_entityBoundsVbo->deactivate();
            }
        }

        void MapRenderer::validateChangedEntities(RenderContext& context) {
            const vector<Model::Entity*>& changedEntities = m_changeSet.changedEntities();
            if (!changedEntities.empty()) {
                m_selectedEntityBoundsVbo->activate();
                m_selectedEntityBoundsVbo->map();

                vector<Entity*> unselectedEntities; // is this still necessary?
                for (unsigned int i = 0; i < changedEntities.size(); i++) {
                    Model::Entity* entity = changedEntities[i];
                    if (context.filter.entityVisible(*entity)) {
                        VboBlock* block = entity->vboBlock();
                        if (m_entityBoundsVbo->ownsBlock(*block))
                            unselectedEntities.push_back(entity);
                        else
                            writeEntityBounds(context, *entity, *block);
                    }
                }

                m_selectedEntityBoundsVbo->unmap();
                m_selectedEntityBoundsVbo->deactivate();

                if (!unselectedEntities.empty()) {
                    m_entityBoundsVbo->activate();
                    m_entityBoundsVbo->map();

                    for (unsigned int i = 0; i < unselectedEntities.size(); i++) {
                        Model::Entity* entity = unselectedEntities[i];
                        if (context.filter.entityVisible(*entity)) {
                            VboBlock* block = entity->vboBlock();
                            writeEntityBounds(context, *entity, *block);
                        }
                    }

                    m_entityBoundsVbo->unmap();
                    m_entityBoundsVbo->deactivate();
                }
            }
        }

        void MapRenderer::validateAddedBrushes(RenderContext& context) {
            const vector<Model::Brush*>& addedBrushes = m_changeSet.addedBrushes();
            if (!addedBrushes.empty()) {
                m_faceVbo->activate();
                m_faceVbo->map();

                for (unsigned int i = 0; i < addedBrushes.size(); i++) {
                    vector<Model::Face*> addedFaces = addedBrushes[i]->faces();
                    for (unsigned int j = 0; j < addedFaces.size(); j++) {
                        Model::Face* face = addedFaces[j];
                        int blockSize = static_cast<int>(face->vertices().size()) * (TexCoordSize + TexCoordSize + ColorSize + ColorSize + VertexSize);
                        VboBlock& block = m_faceVbo->allocBlock(blockSize);
                        writeFaceVertices(context, *face, block);
                        face->setVboBlock(&block);
                    }
                }

                m_faceVbo->unmap();
                m_faceVbo->deactivate();
            }
        }

        void MapRenderer::validateRemovedBrushes(RenderContext& context) {
        }

        void MapRenderer::validateChangedBrushes(RenderContext& context) {
            const vector<Model::Brush*>& changedBrushes = m_changeSet.changedBrushes();
            if (!changedBrushes.empty()) {
                m_faceVbo->activate();
                m_faceVbo->map();
                
                for (unsigned int i = 0; i < changedBrushes.size(); i++) {
                    Model::Brush* brush = changedBrushes[i];
                    for (unsigned int j = 0; j < brush->faces().size(); j++) {
                        Model::Face* face = brush->faces()[j];
                        unsigned int blockSize = static_cast<unsigned int>(face->vertices().size()) * (TexCoordSize + TexCoordSize + ColorSize + ColorSize + VertexSize);
                        VboBlock* block = face->vboBlock();
                        if (block == NULL || block->capacity != blockSize) {
                            block = &m_faceVbo->allocBlock(blockSize);
                            face->setVboBlock(block);
                        }
                        
                        writeFaceVertices(context, *face, *block);
                    }
                }
                
                m_faceVbo->unmap();
                m_faceVbo->deactivate();
            }
        }

        void MapRenderer::validateChangedFaces(RenderContext& context) {
            const vector<Model::Face*> changedFaces = m_changeSet.changedFaces();
            if (!changedFaces.empty()) {
                m_faceVbo->activate();
                m_faceVbo->map();
                for (unsigned int i = 0; i < changedFaces.size(); i++) {
                    Model::Face* face = changedFaces[i];
                    unsigned int blockSize = static_cast<unsigned int>(face->vertices().size()) * (TexCoordSize + TexCoordSize + ColorSize + ColorSize + VertexSize);
                    VboBlock* block = face->vboBlock();
                    if (block == NULL || block->capacity != blockSize) {
                        block = &m_faceVbo->allocBlock(blockSize);
                        face->setVboBlock(block);
                    }
                    
                    writeFaceVertices(context, *face, *block);
                }
                m_faceVbo->unmap();
                m_faceVbo->deactivate();
            }
        }

        void MapRenderer::validateSelection(RenderContext& context) {
            const vector<Model::Entity*> selectedEntities = m_changeSet.selectedEntities();
            const vector<Model::Brush*> selectedBrushes = m_changeSet.selectedBrushes();
            const vector<Model::Face*> selectedFaces = m_changeSet.selectedFaces();

            if (!selectedEntities.empty()) {
                m_selectedEntityBoundsVbo->activate();
                m_selectedEntityBoundsVbo->map();

                for (unsigned int i = 0; i < selectedEntities.size(); i++) {
                    Model::Entity* entity = selectedEntities[i];
                    if (context.filter.entityVisible(*entity)) {
                        VboBlock& block = m_selectedEntityBoundsVbo->allocBlock(6 * 4 * (ColorSize + VertexSize));
                        writeEntityBounds(context, *entity, block);
                        entity->setVboBlock(&block);
                        m_entityBoundsVertexCount -= 6 * 4;
                        m_selectedEntityBoundsVertexCount += 6 * 4;

                        EntityRenderers::iterator it = m_entityRenderers.find(entity);
                        if (it != m_entityRenderers.end()) {
                            m_selectedEntityRenderers[entity] = it->second;
                            m_entityRenderers.erase(it);
                        } else {
                            EntityRenderer* renderer = m_entityRendererManager->entityRenderer(*entity, m_editor.map().mods());
                            if (renderer != NULL)
                                m_selectedEntityRenderers[entity] = renderer;
                        }

                        m_classnameRenderer->transferString(entity->uniqueId(), *m_selectedClassnameRenderer);
                    }
                }

                m_selectedEntityBoundsVbo->unmap();
                m_selectedEntityBoundsVbo->deactivate();

                m_entityBoundsVbo->activate();
                m_entityBoundsVbo->map();
                m_entityBoundsVbo->pack();
                m_entityBoundsVbo->unmap();
                m_entityBoundsVbo->deactivate();
            }
        }

        void MapRenderer::validateDeselection(RenderContext& context) {
            const vector<Model::Entity*> deselectedEntities = m_changeSet.deselectedEntities();
            const vector<Model::Brush*> deselectedBrushes = m_changeSet.deselectedBrushes();
            const vector<Model::Face*> deselectedFaces = m_changeSet.deselectedFaces();

            if (!deselectedEntities.empty()) {
                m_entityBoundsVbo->activate();
                m_entityBoundsVbo->map();

                for (unsigned int i = 0; i < deselectedEntities.size(); i++) {
                    Model::Entity* entity = deselectedEntities[i];
                    if (context.filter.entityVisible(*entity)) {
                        VboBlock& block = m_entityBoundsVbo->allocBlock(6 * 4 * (ColorSize + VertexSize));
                        writeEntityBounds(context, *entity, block);
                        entity->setVboBlock(&block);
                        m_entityBoundsVertexCount += 6 * 4;
                        m_selectedEntityBoundsVertexCount -= 6 * 4;

                        EntityRenderers::iterator it = m_selectedEntityRenderers.find(entity);
                        if (it != m_selectedEntityRenderers.end()) {
                            m_entityRenderers[entity] = it->second;
                            m_selectedEntityRenderers.erase(it);
                        } else {
                            EntityRenderer* renderer = m_entityRendererManager->entityRenderer(*entity, m_editor.map().mods());
                            if (renderer != NULL)
                                m_entityRenderers[entity] = renderer;
                        }

                        m_selectedClassnameRenderer->transferString(entity->uniqueId(), *m_classnameRenderer);
                    }
                }

                m_entityBoundsVbo->unmap();
                m_entityBoundsVbo->deactivate();

                m_selectedEntityBoundsVbo->activate();
                m_selectedEntityBoundsVbo->map();
                m_selectedEntityBoundsVbo->pack();
                m_selectedEntityBoundsVbo->unmap();
                m_selectedEntityBoundsVbo->deactivate();
            }
        }

        void MapRenderer::validate(RenderContext& context) {
            validateEntityRendererCache(context);
            validateAddedEntities(context);
            validateAddedBrushes(context);
            validateSelection(context);
            validateChangedEntities(context);
            validateChangedBrushes(context);
            validateChangedFaces(context);
            validateDeselection(context);
            validateRemovedEntities(context);
            validateRemovedBrushes(context);

            if (!m_changeSet.addedBrushes().empty() ||
                !m_changeSet.removedBrushes().empty() ||
                !m_changeSet.selectedBrushes().empty() ||
                !m_changeSet.deselectedBrushes().empty() ||
                !m_changeSet.selectedFaces().empty() ||
                !m_changeSet.deselectedFaces().empty() ||
                m_changeSet.filterChanged() ||
                m_changeSet.textureManagerChanged()) {
                rebuildFaceIndexBuffers(context);
            }

            if (!m_changeSet.changedBrushes().empty() ||
                !m_changeSet.changedFaces().empty() ||
                !m_changeSet.selectedBrushes().empty() ||
                !m_changeSet.deselectedBrushes().empty() ||
                !m_changeSet.selectedFaces().empty() ||
                !m_changeSet.deselectedFaces().empty() ||
                m_changeSet.filterChanged() ||
                m_changeSet.textureManagerChanged()) {
                rebuildSelectedFaceIndexBuffers(context);
            }

            m_changeSet.clear();
        }

        void MapRenderer::renderSelectionGuides(RenderContext& context, const Vec4f& color) {
            FontManager& fontManager = m_fontManager;

            const Vec3f cameraPos = context.camera.position();
            Vec3f center = m_selectionBounds.center();
            Vec3f size = m_selectionBounds.size();
            Vec3f diff = center - cameraPos;

            unsigned int maxi = 3;
            Vec3f gv[3][4];
            // X guide
            if (diff.y >= 0) {
                gv[0][0] = m_selectionBounds.min;
                gv[0][0].y -= 5;
                gv[0][1] = gv[0][0];
                gv[0][1].y -= 5;
                gv[0][2] = gv[0][1];
                gv[0][2].x = m_selectionBounds.max.x;
                gv[0][3] = gv[0][0];
                gv[0][3].x = m_selectionBounds.max.x;
            } else {
                gv[0][0] = m_selectionBounds.min;
                gv[0][0].y = m_selectionBounds.max.y + 5;
                gv[0][1] = gv[0][0];
                gv[0][1].y += 5;
                gv[0][2] = gv[0][1];
                gv[0][2].x = m_selectionBounds.max.x;
                gv[0][3] = gv[0][0];
                gv[0][3].x = m_selectionBounds.max.x;
            }

            // Y guide
            if (diff.x >= 0) {
                gv[1][0] = m_selectionBounds.min;
                gv[1][0].x -= 5;
                gv[1][1] = gv[1][0];
                gv[1][1].x -= 5;
                gv[1][2] = gv[1][1];
                gv[1][2].y = m_selectionBounds.max.y;
                gv[1][3] = gv[1][0];
                gv[1][3].y = m_selectionBounds.max.y;
            } else {
                gv[1][0] = m_selectionBounds.min;
                gv[1][0].x = m_selectionBounds.max.x + 5;
                gv[1][1] = gv[1][0];
                gv[1][1].x += 5;
                gv[1][2] = gv[1][1];
                gv[1][2].y = m_selectionBounds.max.y;
                gv[1][3] = gv[1][0];
                gv[1][3].y = m_selectionBounds.max.y;
            }

            if (diff.z >= 0)
                for (unsigned int i = 0; i < 2; i++)
                    for (unsigned int j = 0; j < 4; j++)
                        gv[i][j].z = m_selectionBounds.max.z;

            // Z Guide
            if (cameraPos.x <= m_selectionBounds.min.x && cameraPos.y <= m_selectionBounds.max.y) {
                gv[2][0] = m_selectionBounds.min;
                gv[2][0].x -= 3.5f;
                gv[2][0].y = m_selectionBounds.max.y + 3.5f;
                gv[2][1] = gv[2][0];
                gv[2][1].x -= 3.5f;
                gv[2][1].y += 3.5f;
                gv[2][2] = gv[2][1];
                gv[2][2].z = m_selectionBounds.max.z;
                gv[2][3] = gv[2][0];
                gv[2][3].z = m_selectionBounds.max.z;
            } else if (cameraPos.x <= m_selectionBounds.max.x && cameraPos.y >= m_selectionBounds.max.y) {
                gv[2][0] = m_selectionBounds.max;
                gv[2][0].x += 3.5f;
                gv[2][0].y += 3.5f;
                gv[2][1] = gv[2][0];
                gv[2][1].x += 3.5f;
                gv[2][1].y += 3.5f;
                gv[2][2] = gv[2][1];
                gv[2][2].z = m_selectionBounds.min.z;
                gv[2][3] = gv[2][0];
                gv[2][3].z = m_selectionBounds.min.z;
            } else if (cameraPos.x >= m_selectionBounds.max.x && cameraPos.y >= m_selectionBounds.min.y) {
                gv[2][0] = m_selectionBounds.max;
                gv[2][0].y = m_selectionBounds.min.y;
                gv[2][0].x += 3.5f;
                gv[2][0].y -= 3.5f;
                gv[2][1] = gv[2][0];
                gv[2][1].x += 3.5f;
                gv[2][1].y -= 3.5f;
                gv[2][2] = gv[2][1];
                gv[2][2].z = m_selectionBounds.min.z;
                gv[2][3] = gv[2][0];
                gv[2][3].z = m_selectionBounds.min.z;
            } else if (cameraPos.x >= m_selectionBounds.min.x && cameraPos.y <= m_selectionBounds.min.y) {
                gv[2][0] = m_selectionBounds.min;
                gv[2][0].x -= 3.5f;
                gv[2][0].y -= 3.5f;
                gv[2][1] = gv[2][0];
                gv[2][1].x -= 3.5f;
                gv[2][1].y -= 3.5f;
                gv[2][2] = gv[2][1];
                gv[2][2].z = m_selectionBounds.max.z;
                gv[2][3] = gv[2][0];
                gv[2][3].z = m_selectionBounds.max.z;
            } else {
                // above, inside or below, don't render Z guide
                maxi = 2;
            }

            // initialize the stencil buffer to cancel out the guides in those areas where the strings will be rendered
            glPolygonMode(GL_FRONT, GL_FILL);
            glClear(GL_STENCIL_BUFFER_BIT);
            glColorMask(false, false, false, false);
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_ALWAYS, 1, 1);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

			bool depth = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
            if (depth)
                glDisable(GL_DEPTH_TEST);

            Vec3f points[3];
            for (unsigned int i = 0; i < maxi; i++) {
                points[i] = (gv[i][2] - gv[i][1]) / 2 + gv[i][1];

                /*
                
                float dist = context.camera.distanceTo(points[i]);
                float factor = dist / 300;
                
                
                float width = m_guideStrings[i]->width;
                float height = m_guideStrings[i]->height;

                glPushMatrix();
                glTranslatef(points[i].x, points[i].y, points[i].z);
                context.camera.setBillboard();
                glScalef(factor, factor, 0);
                glTranslatef(-width / 2, -height / 2, 0);
                m_guideStrings[i]->renderBackground(1, 1);
                glPopMatrix();
                 */
            }

            glColorMask(true, true, true, true);
            glStencilFunc(GL_NOTEQUAL, 1, 1);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

            if (depth)
                glEnable(GL_DEPTH_TEST);

            for (unsigned int i = 0; i < 3; i++) {
                glColorV4f(color);

                glBegin(GL_LINE_STRIP);
                for (unsigned int j = 0; j < 4; j++)
                    glVertexV3f(gv[i][j]);
                glEnd();
            }

            glDisable(GL_STENCIL_TEST);

            /*
            fontManager.activate();
            for (unsigned int i = 0; i < maxi; i++) {
                glColorV4f(color);

                float dist = context.camera.distanceTo(points[i]);
                float factor = dist / 300;
                float width = m_guideStrings[i]->width;
                float height = m_guideStrings[i]->height;

                glPushMatrix();
                glTranslatef(points[i].x, points[i].y, points[i].z);
                context.camera.setBillboard();
                glScalef(factor, factor, 0);
                glTranslatef(-width / 2, -height / 2, 0);
                m_guideStrings[i]->render();
                glPopMatrix();
            }
            fontManager.deactivate();
             */
        }

        void MapRenderer::renderEntityBounds(RenderContext& context, const Vec4f* color, int vertexCount) {
			if (vertexCount == 0)
				return;

            glSetEdgeOffset(0.5f);

            glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
            if (color != NULL) {
                glColorV4f(*color);
                glVertexPointer(3, GL_FLOAT, ColorSize + VertexSize, (const GLvoid *)(long)ColorSize);
            } else {
                glInterleavedArrays(GL_C4UB_V3F, 0, 0);
            }

            glDrawArrays(GL_LINES, 0, vertexCount);

            glPopClientAttrib();
            glResetEdgeOffset();
        }

        void MapRenderer::renderEntityModels(RenderContext& context, EntityRenderers& entities) {
			if (entities.empty())
				return;

            m_entityRendererManager->activate();

            glMatrixMode(GL_MODELVIEW);
            EntityRenderers::iterator it;
            for (it = entities.begin(); it != entities.end(); ++it) {
                Entity* entity = it->first;
                EntityRenderer* renderer = it->second;
                glPushMatrix();
                renderer->render(context, *entity);
                glPopMatrix();
            }

            glDisable(GL_TEXTURE_2D);
            m_entityRendererManager->deactivate();
        }

        void MapRenderer::renderEdges(RenderContext& context, const Vec4f* color, const VboBlock* indexBlock) {
            if (indexBlock == NULL)
                return;
            
            glDisable(GL_TEXTURE_2D);
            glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);

            if (color != NULL) {
                glColorV4f(*color);
                glVertexPointer(3, GL_FLOAT, TexCoordSize + TexCoordSize + ColorSize + ColorSize + VertexSize, (const GLvoid *)(long)(TexCoordSize + TexCoordSize + ColorSize + ColorSize));
            } else {
                glEnableClientState(GL_COLOR_ARRAY);
                glColorPointer(4, GL_UNSIGNED_BYTE, TexCoordSize + TexCoordSize + ColorSize + ColorSize + VertexSize, (const GLvoid *)(long)(TexCoordSize + TexCoordSize));
                glVertexPointer(3, GL_FLOAT, TexCoordSize + TexCoordSize + ColorSize + ColorSize + VertexSize, (const GLvoid *)(long)(TexCoordSize + TexCoordSize + ColorSize + ColorSize));
            }

            glDrawElements(GL_LINES, indexBlock->capacity / sizeof(unsigned int), GL_UNSIGNED_INT, reinterpret_cast<GLvoid*>(indexBlock->address));
            glPopClientAttrib();
        }

        void MapRenderer::renderFaces(RenderContext& context, bool textured, bool selected, const FaceIndexBlocks& indexBlocks) {
            if (indexBlocks.empty())
                return;

            glPolygonMode(GL_FRONT, GL_FILL);
            glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);

            if (context.options.renderGrid) {
                Controller::Grid& grid = m_editor.grid();
                glActiveTexture(GL_TEXTURE2);
                glEnable(GL_TEXTURE_2D);
                m_gridRenderer->activate(grid);
                glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
                glClientActiveTexture(GL_TEXTURE2);
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glTexCoordPointer(2, GL_FLOAT, TexCoordSize + TexCoordSize + ColorSize + ColorSize + VertexSize, (const GLvoid *)0);
            }

            if (selected) {
                if (m_selectionDummyTexture == NULL) {
                    unsigned char image = 0;
                    m_selectionDummyTexture = new Model::Assets::Texture("selection dummy", &image, 1, 1);
                }

                const Vec4f& selectedFaceColor = context.preferences.selectedFaceColor();
                GLfloat color[4] = {selectedFaceColor.x, selectedFaceColor.y, selectedFaceColor.z, selectedFaceColor.w};

                glActiveTexture(GL_TEXTURE1);
                glEnable(GL_TEXTURE_2D);
                m_selectionDummyTexture->activate();
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
                glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
                glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
                glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, color);
                glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_PREVIOUS);
                glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_PREVIOUS);
                glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_CONSTANT);
                glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 2);
            }

            glActiveTexture(GL_TEXTURE0);
            if (textured) {
                glEnable(GL_TEXTURE_2D);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);

                float brightness = context.preferences.brightness();
                float color[4] = {brightness / 2, brightness / 2, brightness / 2, 1};

                glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
                glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
                glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, color);
                glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_TEXTURE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_TEXTURE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_CONSTANT);
                glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 2.0f);

                glClientActiveTexture(GL_TEXTURE0);
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glTexCoordPointer(2, GL_FLOAT, TexCoordSize + TexCoordSize + ColorSize + ColorSize + VertexSize, (const GLvoid *)(long)TexCoordSize);
            } else {
                glDisable(GL_TEXTURE_2D);
            }

            glEnableClientState(GL_COLOR_ARRAY);
            glColorPointer(4, GL_UNSIGNED_BYTE, TexCoordSize + TexCoordSize + ColorSize + ColorSize + VertexSize, (const GLvoid *)(long)(TexCoordSize + TexCoordSize + ColorSize));
            glVertexPointer(3, GL_FLOAT, TexCoordSize + TexCoordSize + ColorSize + ColorSize + VertexSize, (const GLvoid *)(long)(TexCoordSize + TexCoordSize + ColorSize + ColorSize));

            FaceIndexBlocks::const_iterator it;
            for (it = indexBlocks.begin(); it != indexBlocks.end(); ++it) {
                Model::Assets::Texture* texture = it->first;
                VboBlock* block = it->second;
                if (textured) texture->activate();
                glDrawElements(GL_TRIANGLES, block->capacity / sizeof(unsigned int), GL_UNSIGNED_INT, reinterpret_cast<GLvoid*>(block->address));
                if (textured) texture->deactivate();
            }
            
            if (textured)
                glDisable(GL_TEXTURE_2D);

            if (selected) {
                glActiveTexture(GL_TEXTURE1);
                m_selectionDummyTexture->deactivate();
                glDisable(GL_TEXTURE_2D);
            }

            if (context.options.renderGrid) {
                glActiveTexture(GL_TEXTURE2);
                m_gridRenderer->deactivate();
                glDisable(GL_TEXTURE_2D);
                glActiveTexture(GL_TEXTURE0);
            }

            glPopClientAttrib();
        }

        void MapRenderer::renderFigures(RenderContext& context) {
            for (unsigned int i = 0; i < m_figures.size(); i++)
                m_figures[i]->render(context);
        }

        MapRenderer::MapRenderer(Controller::Editor& editor, FontManager& fontManager) : m_editor(editor), m_fontManager(fontManager), m_edgeIndexBlock(NULL), m_selectedEdgeIndexBlock(NULL) {
            Model::Preferences& prefs = *Model::Preferences::sharedPreferences;

            m_faceVbo = new Vbo(GL_ARRAY_BUFFER, 0xFFFF);
            m_faceIndexVbo = new Vbo(GL_ELEMENT_ARRAY_BUFFER, 0xFFFF);
            m_edgeIndexVbo = new Vbo(GL_ELEMENT_ARRAY_BUFFER, 0xFFFF);
            
            m_gridRenderer = new GridRenderer(prefs.gridAlpha());

            m_entityBoundsVbo = new Vbo(GL_ARRAY_BUFFER, 0xFFFF);
            m_selectedEntityBoundsVbo = new Vbo(GL_ARRAY_BUFFER, 0xFFFF);
            m_entityBoundsVertexCount = 0;
            m_selectedEntityBoundsVertexCount = 0;

            m_entityRendererManager = new EntityRendererManager(prefs.quakePath(), m_editor.palette());
            m_entityRendererCacheValid = true;

            m_classnameRenderer = new TextRenderer(m_fontManager, prefs.infoOverlayFadeDistance());
            m_selectedClassnameRenderer = new TextRenderer(m_fontManager, prefs.selectedInfoOverlayFadeDistance());

            m_selectionDummyTexture = NULL;
            m_editor.setRenderer(this);

            Model::Map& map = m_editor.map();
            Model::Selection& selection = map.selection();

            map.mapLoaded               += new Model::Map::MapEvent::Listener<MapRenderer>(this, &MapRenderer::mapLoaded);
            map.mapCleared              += new Model::Map::MapEvent::Listener<MapRenderer>(this, &MapRenderer::mapCleared);
            map.propertiesDidChange     += new Model::Map::EntityEvent::Listener<MapRenderer>(this, &MapRenderer::propertiesDidChange);
            map.brushesDidChange        += new Model::Map::BrushEvent::Listener<MapRenderer>(this, &MapRenderer::brushesDidChange);
            map.facesDidChange          += new Model::Map::FaceEvent::Listener<MapRenderer>(this, &MapRenderer::facesDidChange);
            selection.selectionAdded    += new Model::Selection::SelectionEvent::Listener<MapRenderer>(this, &MapRenderer::selectionAdded);
            selection.selectionRemoved  += new Model::Selection::SelectionEvent::Listener<MapRenderer>(this, &MapRenderer::selectionRemoved);

            addEntities(map.entities());
        }

        MapRenderer::~MapRenderer() {
            m_editor.setRenderer(NULL);
            
            Model::Map& map = m_editor.map();
            Model::Selection& selection = map.selection();
            
            map.mapLoaded               -= new Model::Map::MapEvent::Listener<MapRenderer>(this, &MapRenderer::mapLoaded);
            map.mapCleared              -= new Model::Map::MapEvent::Listener<MapRenderer>(this, &MapRenderer::mapCleared);
            map.propertiesDidChange     -= new Model::Map::EntityEvent::Listener<MapRenderer>(this, &MapRenderer::propertiesDidChange);
            map.brushesDidChange        -= new Model::Map::BrushEvent::Listener<MapRenderer>(this, &MapRenderer::brushesDidChange);
            map.facesDidChange          -= new Model::Map::FaceEvent::Listener<MapRenderer>(this, &MapRenderer::facesDidChange);
            selection.selectionAdded    -= new Model::Selection::SelectionEvent::Listener<MapRenderer>(this, &MapRenderer::selectionAdded);
            selection.selectionRemoved  -= new Model::Selection::SelectionEvent::Listener<MapRenderer>(this, &MapRenderer::selectionRemoved);

            const vector<Entity*>& entities = map.entities();
            for (unsigned int i = 0; i < entities.size(); i++) {
                Entity* entity = entities[i];
                const vector<Brush*>& brushes = entity->brushes();
                for (unsigned int j = 0; j < brushes.size(); j++) {
                    Brush* brush = brushes[j];
                    const vector<Face*>& faces = brush->faces();
                    for (unsigned int k = 0; k < faces.size(); k++)
                        faces[k]->setVboBlock(NULL);
                }
                entity->setVboBlock(NULL);
            }
            
            delete m_faceVbo;
            delete m_faceIndexVbo;
            delete m_edgeIndexVbo;
            
            delete m_gridRenderer;

            delete m_entityBoundsVbo;
            delete m_selectedEntityBoundsVbo;
            delete m_entityRendererManager;

            delete m_classnameRenderer;
            delete m_selectedClassnameRenderer;

            if (m_selectionDummyTexture != NULL)
                delete m_selectionDummyTexture;
        }

        void MapRenderer::addFigure(Figure& figure) {
            m_figures.push_back(&figure);
        }
        
        void MapRenderer::removeFigure(Figure& figure) {
            vector<Figure*>::iterator it = find(m_figures.begin(), m_figures.end(), &figure);
            if (it != m_figures.end())
                m_figures.erase(it);
        }

        void MapRenderer::render(RenderContext& context) {
            validate(context);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glFrontFace(GL_CW);
            glEnable(GL_CULL_FACE);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glShadeModel(GL_SMOOTH);
            glResetEdgeOffset();

            if (context.options.renderOrigin) {
                glDisable(GL_TEXTURE_2D);
                glBegin(GL_LINES);
                glColor4f(1, 0, 0, 0.5f);
                glVertex3f(-context.options.originAxisLength, 0, 0);
                glVertex3f(context.options.originAxisLength, 0, 0);
                glColor4f(0, 1, 0, 0.5f);
                glVertex3f(0, -context.options.originAxisLength, 0);
                glVertex3f(0, context.options.originAxisLength, 0);
                glColor4f(0, 0, 1, 0.5f);
                glVertex3f(0, 0, -context.options.originAxisLength);
                glVertex3f(0, 0, context.options.originAxisLength);
                glEnd();
            }

//            glColor4f(1, 1, 1, 1);
//
//            glMatrixMode(GL_MODELVIEW);
//            glPushMatrix();
//            m_editor.camera().setBillboard();
//
//            FontDescriptor descriptor("Arial", 13);
//            StringRenderer& renderer = m_fontManager.createStringRenderer(descriptor, "test");
//            m_fontManager.activate();
//            glTranslatef(100, 200, 0);
//            renderer.render();
//            m_fontManager.deactivate();
//            m_fontManager.destroyStringRenderer(renderer);
//            glPopMatrix();

            if (context.options.renderBrushes) {
                m_faceVbo->activate();
                m_faceIndexVbo->activate();
                glEnableClientState(GL_VERTEX_ARRAY);
                glEnableClientState(GL_INDEX_ARRAY);

                switch (context.options.renderMode) {
                    case Controller::TB_RM_TEXTURED:
                        if (context.options.isolationMode == Controller::IM_NONE)
                            renderFaces(context, true, false, m_faceIndexBlocks);
                        if (!m_editor.map().selection().empty())
                            renderFaces(context, true, true, m_selectedFaceIndexBlocks);
                        break;
                    case Controller::TB_RM_FLAT:
                        if (context.options.isolationMode == Controller::IM_NONE)
                            renderFaces(context, false, false, m_faceIndexBlocks);
                        if (!m_editor.map().selection().empty())
                            renderFaces(context, false, true, m_selectedFaceIndexBlocks);
                        break;
                    case Controller::TB_RM_WIREFRAME:
                        break;
                }

                m_faceIndexVbo->deactivate();
                m_edgeIndexVbo->activate();
                
                if (context.options.isolationMode != Controller::IM_DISCARD) {
                    glSetEdgeOffset(0.1f);
                    renderEdges(context, NULL, m_edgeIndexBlock);
                    glResetEdgeOffset();
                }

                if (!m_editor.map().selection().empty()) {
                    glDisable(GL_DEPTH_TEST);
                    renderEdges(context, &context.preferences.hiddenSelectedEdgeColor(), m_selectedEdgeIndexBlock);
                    glEnable(GL_DEPTH_TEST);

                    glSetEdgeOffset(0.2f);
                    glDepthFunc(GL_LEQUAL);
                    renderEdges(context, &context.preferences.selectedEdgeColor(), m_selectedEdgeIndexBlock);
                    glDepthFunc(GL_LESS);
                    glResetEdgeOffset();
                }

                glDisableClientState(GL_INDEX_ARRAY);
                glDisableClientState(GL_VERTEX_ARRAY);
                m_edgeIndexVbo->deactivate();
                m_faceVbo->deactivate();
            }

            if (context.options.renderEntities) {
                if (context.options.isolationMode == Controller::IM_NONE) {
                    m_entityBoundsVbo->activate();
                    glEnableClientState(GL_VERTEX_ARRAY);
                    renderEntityBounds(context, NULL, m_entityBoundsVertexCount);
                    glDisableClientState(GL_VERTEX_ARRAY);
                    m_entityBoundsVbo->deactivate();

                    renderEntityModels(context, m_entityRenderers);

                    if (context.options.renderEntityClassnames) {
                        m_classnameRenderer->render(context, context.preferences.infoOverlayColor());
                    }
                } else if (context.options.isolationMode == Controller::IM_WIREFRAME) {
                    m_entityBoundsVbo->activate();
                    glEnableClientState(GL_VERTEX_ARRAY);
                    renderEntityBounds(context, &context.preferences.entityBoundsWireframeColor(), m_entityBoundsVertexCount);
                    glDisableClientState(GL_VERTEX_ARRAY);
                    m_entityBoundsVbo->deactivate();
                }

                if (!m_editor.map().selection().empty()) {
                    if (context.options.renderEntityClassnames) {
                        m_selectedClassnameRenderer->render(context, context.preferences.selectedInfoOverlayColor());
                    }

                    m_selectedEntityBoundsVbo->activate();
                    glEnableClientState(GL_VERTEX_ARRAY);

                    glDisable(GL_CULL_FACE);
                    glDisable(GL_DEPTH_TEST);
                    renderEntityBounds(context, &context.preferences.hiddenSelectedEntityBoundsColor(), m_selectedEntityBoundsVertexCount);
                    glEnable(GL_DEPTH_TEST);
                    glDepthFunc(GL_LEQUAL);
                    renderEntityBounds(context, &context.preferences.selectedEntityBoundsColor(), m_selectedEntityBoundsVertexCount);
                    glDepthFunc(GL_LESS);
                    glEnable(GL_CULL_FACE);

                    glDisableClientState(GL_VERTEX_ARRAY);
                    m_selectedEntityBoundsVbo->deactivate();

                    renderEntityModels(context, m_selectedEntityRenderers);

                    if (context.options.renderSizeGuides) {
                        glDisable(GL_DEPTH_TEST);
                        renderSelectionGuides(context, context.preferences.selectionGuideColor());
                    }
                }
            }
            
            renderFigures(context);
        }
    }
}
