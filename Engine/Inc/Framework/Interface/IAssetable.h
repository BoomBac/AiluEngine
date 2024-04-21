#pragma once
#ifndef __IASSETABLE_H__
#define __IASSETABLE_H__
#include "Framework/Math/Guid.h"
namespace Ailu
{
	class Asset;
	class IAssetable
	{
	public:
		virtual ~IAssetable() = default;
		virtual const Guid& GetGuid() const = 0;
		virtual void AttachToAsset(Asset* owner) = 0;
	};
}

#endif // !IASSETABLE_H__

