#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <RE/A/Actor.h>
#include <RE/B/BSCoreTypes.h>

#include <toml++/toml.h>

#include "ConfigKey/BoolConfigKey.hpp"
#include "ConfigKey/EnumConfigKey.hpp"
#include "ConfigKey/IntConfigKey.hpp"
#include "DllDependencyKey.hpp"
#include "GlobalVarForm.hpp"
#include "SoulGemGroup.hpp"
#include "SoulGemMap.hpp"
#include "../SoulSize.hpp"

namespace RE {
    class TESDataHandler;
    class TESGlobal;
    class TESSoulGem;
} // namespace RE

class YASTMConfig {
public:
    class Snapshot;
    using SoulGemGroupList = std::vector<SoulGemGroup>;
    template <typename KeyType>
    using GlobalVarMap = std::unordered_map<KeyType, GlobalVarForm<KeyType>>;

private:
    GlobalVarMap<BoolConfigKey> globalBools_;
    GlobalVarMap<EnumConfigKey> globalEnums_;
    GlobalVarMap<IntConfigKey> globalInts_;

    SoulGemGroupList soulGemGroupList_;
    SoulGemMap soulGemMap_;

    std::unordered_map<DLLDependencyKey, const SKSE::PluginInfo*> dependencies_;
    mutable std::mutex mutex_;

    explicit YASTMConfig();

    /**
     * @brief Read and parse configuration files.
     */
    void loadConfigFiles_();
    /**
     * @brief Load game forms according to configuration. Call this *after*
     * loadConfigFiles_().
     */
    void loadGameForms_(RE::TESDataHandler* dataHandler);

    void loadYASTMConfigFile_();
    void loadIndividualConfigFiles_();
    std::size_t readAndCountSoulGemGroupConfigs_(const toml::table& table);

    void loadGlobalForms_(RE::TESDataHandler* dataHandler);
    void createSoulGemMap_(RE::TESDataHandler* dataHandler);

public:
    YASTMConfig(const YASTMConfig&) = delete;
    YASTMConfig(YASTMConfig&&) = delete;
    YASTMConfig& operator=(const YASTMConfig&) = delete;
    YASTMConfig& operator=(YASTMConfig&&) = delete;

    static YASTMConfig& getInstance()
    {
        static YASTMConfig instance;

        return instance;
    }

    // These three functions needs to be called manually at different times.
    void checkDllDependencies(const SKSE::LoadInterface* loadInterface);

    void loadConfig(RE::TESDataHandler* dataHandler);

    /**
     * @brief Clears (most) data stored in YASTMConfig.
     */
    void clear();

    bool isDllLoaded(const DLLDependencyKey key) const
    {
        return dependencies_.contains(key) && dependencies_.at(key) != nullptr;
    }

    float getGlobalValue(const BoolConfigKey key) const
    {
        return globalBools_.at(key).value();
    }
    float getGlobalValue(const EnumConfigKey key) const
    {
        return globalEnums_.at(key).value();
    }
    float getGlobalValue(const IntConfigKey key) const
    {
        return globalInts_.at(key).value();
    }

    bool getGlobalBool(const BoolConfigKey key) const
    {
        return getGlobalValue(key) != 0;
    }
    int getGlobalInt(const IntConfigKey key) const
    {
        return static_cast<int>(getGlobalValue(key));
    }

    template <EnumConfigKey key>
    auto getGlobalEnum() const
    {
        return EnumConfigKeyTypeMap<key>()(getGlobalValue(key));
    }

    const SoulGemMap& soulGemMap() const noexcept { return soulGemMap_; }

    /**
     * @brief Represents a snapshot of the configuration at a certain point in
     * time.
     */
    class Snapshot {
        std::bitset<static_cast<std::size_t>(BoolConfigKey::Count)>
            configBools_;
        std::unordered_map<EnumConfigKey, EnumConfigUnderlyingType>
            configEnums_;
        std::unordered_map<IntConfigKey, int> configInts_;

    public:
        explicit Snapshot(const YASTMConfig& config);

        template <EnumConfigKey K>
        auto get() const;

        bool operator[](BoolConfigKey key) const;
        int operator[](IntConfigKey key) const;
    };
};

inline YASTMConfig::Snapshot::Snapshot(const YASTMConfig& config)
{
    forEachBoolConfigKey([&, this](const BoolConfigKey key) {
        configBools_[static_cast<std::size_t>(key)] = config.getGlobalBool(key);
    });

    forEachEnumConfigKey([&, this](const EnumConfigKey key) {
        configEnums_.emplace(
            key,
            static_cast<EnumConfigUnderlyingType>(config.getGlobalValue(key)));
    });

    forEachIntConfigKey([&, this](const IntConfigKey key) {
        configInts_.emplace(key, config.getGlobalInt(key));
    });
}

inline bool YASTMConfig::Snapshot::operator[](const BoolConfigKey key) const
{
    return configBools_[static_cast<std::size_t>(key)];
}

inline int YASTMConfig::Snapshot::operator[](const IntConfigKey key) const
{
    return configInts_.at(key);
}

template <EnumConfigKey K>
inline auto YASTMConfig::Snapshot::get() const
{
    return static_cast<EnumConfigKeyTypeMap<K>::type>(configEnums_.at(K));
}

class YASTMConfigLoadError : public std::runtime_error {
public:
    explicit YASTMConfigLoadError(const std::string& message)
        : std::runtime_error(message)
    {}
};
