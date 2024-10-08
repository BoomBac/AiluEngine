//
// Created by 22292 on 2024/7/18.
//

#ifndef AILU_PLACEACTORS_H
#define AILU_PLACEACTORS_H

#include "ImGuiWidget.h"

namespace Ailu
{
    namespace Editor
    {
        DECLARE_ENUM(EPlaceActorsType, kObj, 
        kCube, kPlane, kSphere,kCapsule ,
        kDirectionalLight, kPointLight, kSpotLight, kAreaLight, kLightProbe)

        class PlaceActors : public ImGuiWidget
        {
        public:
            inline static const String kDragPlacementObj    = "DragPlacementObj";
            PlaceActors();
            ~PlaceActors();
            void Open(const i32& handle) final;
            void Close(i32 handle) final;
            void OnEvent(Event &e) final;
        private:
            void ShowImpl() final;
        private:
            char _input_buffer[256];
            i16 _group_selected_index = -1;
            Vector<String> _groups = {
                    "Basic","Lights"
            };
        };

    }// namespace Editor
}// namespace Ailu

#endif//AILU_PLACEACTORS_H
