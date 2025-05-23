#pragma once
#ifndef __ASSET_TABLE_H__
#define __ASSET_TABLE_H__
#include "ImGuiWidget.h"

namespace Ailu
{
	namespace Editor
	{
		class AssetTable : public ImGuiWidget
		{
		public:
			AssetTable();
			~AssetTable();
			void Open(const i32& handle) final;
			void Close(i32 handle) final;
		private:
			void ShowImpl() final;
		};
	}
}


#endif // !ASSET_TABLE_H__

