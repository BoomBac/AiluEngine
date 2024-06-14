#pragma once
#ifndef __ENV_SETTING__
#define __ENV_SETTING__

#include "ImGuiWidget.h"

namespace Ailu
{
	namespace Editor
	{
		class EnvironmentSetting : public ImGuiWidget
		{
		public:
			EnvironmentSetting();
			~EnvironmentSetting();
			void Open(const i32& handle) final;
			void Close(i32 handle) final;
		private:
			void ShowImpl() final;
		};
	}
}


#endif // !ENV_SETTING__

