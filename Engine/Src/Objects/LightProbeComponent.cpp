#include "pch.h"
#include "Objects/LightProbeComponent.h"

namespace Ailu
{
    Ailu::LightProbeComponent::LightProbeComponent()
    {
    }
}// namespace Ailu

void Ailu::LightProbeComponent::Tick(const float &delta_time)
{
}

void Ailu::LightProbeComponent::Serialize(std::basic_ostream<char, std::char_traits<char>> &os, String indent)
{
}

void *Ailu::LightProbeComponent::DeserializeImpl(Queue<std::tuple<String, String>> &formated_str)
{
    return nullptr;
}
