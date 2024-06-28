#pragma once
#ifndef __ENV_SETTING__
#define __ENV_SETTING__

#include "ImGuiWidget.h"

namespace Ailu
{
	class Material;
	class PostProcessPass;
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
		private:
			Material* _deferred_lighting_mat;
			PostProcessPass* _post_process_pass;
		};
	}
}


#endif // !ENV_SETTING__

