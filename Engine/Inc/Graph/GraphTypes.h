#pragma once

#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Math/ALMath.hpp"
#include "Framework/Math/Guid.h"
#include "generated/GraphTypes.gen.h"

namespace Ailu
{
    AENUM()
    enum class EGraphPinDirection : u8
    {
        kInput,
        kOutput
    };

    AENUM()
    enum class EGraphPinKind : u8
    {
        kExecution,
        kValue
    };

    AENUM()
    enum class EGraphNodeFlag : u32
    {
        kNone = 0u,
        kCanDelete = 1u << 0u,
        kCanDuplicate = 1u << 1u,
        kCanRename = 1u << 2u,
        kCanCollapse = 1u << 3u,
        kEntryNode = 1u << 4u,
        kPureNode = 1u << 5u
    };

    AENUM()
    enum class EGraphLinkFlag : u32
    {
        kNone = 0u,
        kDisabled = 1u << 0u
    };

    inline u32 GraphFlag(EGraphNodeFlag flag)
    {
        return static_cast<u32>(flag);
    }

    inline u32 GraphFlag(EGraphLinkFlag flag)
    {
        return static_cast<u32>(flag);
    }

    inline bool HasGraphFlag(u32 flags, EGraphNodeFlag flag)
    {
        return (flags & GraphFlag(flag)) != 0u;
    }

    inline bool HasGraphFlag(u32 flags, EGraphLinkFlag flag)
    {
        return (flags & GraphFlag(flag)) != 0u;
    }

    ASTRUCT()
    struct AILU_API GraphPinData
    {
        GENERATED_BODY()

        APROPERTY()
        Guid _id = Guid::EmptyGuid();
        APROPERTY()
        String _name;
        APROPERTY()
        String _value_type;
        APROPERTY()
        EGraphPinDirection _direction = EGraphPinDirection::kInput;
        APROPERTY()
        EGraphPinKind _kind = EGraphPinKind::kValue;
        APROPERTY()
        String _default_value;
        APROPERTY()
        bool _is_hidden = false;
        APROPERTY()
        bool _is_dynamic = false;
    };

    ASTRUCT()
    struct AILU_API GraphNodeData
    {
        GENERATED_BODY()

        APROPERTY()
        Guid _id = Guid::EmptyGuid();
        APROPERTY()
        String _node_type;
        APROPERTY()
        String _display_name;
        APROPERTY()
        Vector2f _position = Vector2f::kZero;
        APROPERTY()
        Vector2f _size = {180.0f, 100.0f};
        APROPERTY()
        Vector<GraphPinData> _pins;
        APROPERTY()
        String _property_data;
        APROPERTY()
        u32 _flags = GraphFlag(EGraphNodeFlag::kCanDelete) | GraphFlag(EGraphNodeFlag::kCanDuplicate);
        APROPERTY()
        bool _is_collapsed = false;
    };

    ASTRUCT()
    struct AILU_API GraphLinkData
    {
        GENERATED_BODY()

        APROPERTY()
        Guid _id = Guid::EmptyGuid();
        APROPERTY()
        Guid _output_pin = Guid::EmptyGuid();
        APROPERTY()
        Guid _input_pin = Guid::EmptyGuid();
        APROPERTY()
        u32 _flags = GraphFlag(EGraphLinkFlag::kNone);
    };

    ASTRUCT()
    struct AILU_API GraphCommentData
    {
        GENERATED_BODY()

        APROPERTY()
        Guid _id = Guid::EmptyGuid();
        APROPERTY()
        String _title = "Comment";
        APROPERTY()
        Vector2f _position = Vector2f::kZero;
        APROPERTY()
        Vector2f _size = {400.0f, 240.0f};
        APROPERTY()
        Color _color = {0.2f, 0.4f, 0.8f, 0.25f};
    };
} // namespace Ailu
