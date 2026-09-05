#pragma once
#include "Assets/TextureArtifact.h"
#include "Framework/Interface/IParser.h"
namespace Ailu { class AILU_API TextureImporter final { public: bool Import(const WString &source_path, const TextureImportSetting &setting, TextureArtifact &out_artifact); }; }
