#include "YASTMConfig.hpp"

#include <filesystem>

#include <toml++/toml.h>

#include <RE/B/BGSDefaultObjectManager.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESGlobal.h>
#include <RE/T/TESSoulGem.h>
#include <SKSE/SKSE.h>

#include "SoulGemGroup.hpp"

void YASTMConfig::_readYASTMConfig()
{
    using namespace std::literals;
    namespace logger = SKSE::log;

    toml::table table;
    std::string_view configPath = "Data/YASTM.toml"sv;

    try {
        table = toml::parse_file(configPath);

        const auto yastmTable = table["YASTM"];

        const auto readIdFromToml = [&](const Key key) {
            const auto& keyName = YASTMConfig::toKeyName(key);

            if (const auto idArray =
                    yastmTable[std::string{keyName} + "Global"].as_array();
                idArray) {
                _globals.insert(std::make_pair(
                    key,
                    GlobalId::constructFromToml(keyName, *idArray)));
            } else {
                logger::warn(
                    FMT_STRING(
                        "Form data for configuration key '{}' not found."sv),
                    keyName);
            }
        };

        readIdFromToml(Key::AllowPartiallyFillingSoulGems);
        readIdFromToml(Key::AllowSoulDisplacement);
        readIdFromToml(Key::AllowSoulRelocation);
        readIdFromToml(Key::AllowSoulShrinking);
    } catch (const toml::parse_error&) {
        logger::warn(
            FMT_STRING("Error while parsing config file \"{}\""sv),
            configPath);
    }

    logger::trace("Loaded configuration from TOML:"sv);

    for (const auto& [key, globalId] : _globals) {
        logger::trace(
            FMT_STRING("- {} = [{:08x}, {}]"sv),
            globalId.keyName(),
            globalId.formId(),
            globalId.pluginName());
    }
}

void YASTMConfig::_readSoulGemConfigs()
{
    using namespace std::literals;
    namespace logger = SKSE::log;

    std::vector<std::filesystem::path> configPaths;

    for (const auto& entry : std::filesystem::directory_iterator("Data/"sv)) {
        if (entry.exists() && !entry.path().empty() &&
            entry.path().extension() == ".toml"sv) {
            const auto fileName = entry.path().filename();
            const auto fileNameStr = fileName.string();

            if (fileNameStr.starts_with("YASTM_"sv)) {
                logger::info(
                    FMT_STRING("Found YASTM configuration file: {}"sv),
                    fileNameStr);
                configPaths.push_back(entry.path());
            }
        }
    }

    if (configPaths.empty()) {
        throw std::runtime_error("No YASTM configuration files found.");
    }

    std::size_t validConfigCount = 0;

    for (const auto& configPath : configPaths) {
        toml::table table;

        std::string configPathStr = configPath.string();

        try {
            table = toml::parse_file(configPathStr);

            if (auto soulGems = table["soulGems"sv].as_array()) {
                for (toml::node& elem : *soulGems) {
                    elem.visit([this](auto&& el) {
                        if constexpr (toml::is_table<decltype(el)>) {
                            _soulGemGroups.push_back(
                                std::make_shared<SoulGemGroup>(
                                    SoulGemGroup::constructFromToml(el)));
                        } else {
                            throw std::runtime_error{
                                "Value of key 'soulGems' must be a table."};
                        }
                    });
                }

                // If we made it here without an error thrown, it's a valid
                // configuration.
                ++validConfigCount;
            }
        } catch (const toml::parse_error&) {
            logger::warn(
                FMT_STRING("Error while parsing config file \"{}\""sv),
                configPathStr);
        }
    }

    // Print the loaded configuration (we can't read the in-game forms yet.
    // Game hasn't fully initialized.)
    logger::trace("Loaded soul gem configuration from TOML:"sv);

    for (const auto& soulGemGroup : _soulGemGroups) {
        logger::trace(
            FMT_STRING("    {} (isReusable={}, capacity={})"sv),
            soulGemGroup->id(),
            soulGemGroup->isReusable(),
            soulGemGroup->capacity());

        for (const auto& soulGemId : soulGemGroup->members()) {
            logger::trace(
                FMT_STRING("        [{:#08x}, {}]"sv),
                soulGemId->formId(),
                soulGemId->pluginName());
        }
    }

    if (validConfigCount <= 0) {
        throw std::runtime_error{"No valid configuration files found."};
    }
}

bool _canHoldBlackSoul(RE::TESSoulGem* const soulGemForm)
{
    return soulGemForm->GetFormFlags() &
           RE::TESSoulGem::RecordFlags::kCanHoldNPCSoul;
}

bool YASTMConfig::_isValidConfig(RE::TESDataHandler* const dataHandler) const
{
    using namespace std::literals;
    namespace logger = SKSE::log;

    logger::info("Loading soul gem forms..."sv);

    const auto defaultObjectManager =
        RE::BGSDefaultObjectManager::GetSingleton();
    const RE::BGSKeyword* reusableSoulGemKeyword =
        defaultObjectManager->GetObject<RE::BGSKeyword>(
            RE::DEFAULT_OBJECT::kKeywordReusableSoulGem);

    for (const auto& soulGemGroup : _soulGemGroups) {
        for (int i = 0; i < soulGemGroup->members().size(); ++i) {
            auto& soulGemId = soulGemGroup->members()[i];

            RE::TESForm* const form = dataHandler->LookupForm(
                soulGemId->formId(),
                soulGemId->pluginName());

            if (form == nullptr) {
                logger::error(
                    FMT_STRING(
                        "Form with ID {:08x} does not exist in file \"{}\""sv),
                    soulGemId->formId(),
                    soulGemId->pluginName());
                return false;
            }

            if (!form->IsSoulGem()) {
                logger::error(
                    FMT_STRING(
                        "Form {:08x} \"{}\" from file \"{}\" is not a soul gem."sv),
                    form->GetFormID(),
                    form->GetName(),
                    soulGemId->pluginName());
                return false;
            }

            RE::TESSoulGem* const soulGemForm = form->As<RE::TESSoulGem>();

            // We use effective capacity since black souls are grand souls
            // in-game.
            if (soulGemGroup->effectiveCapacity() !=
                static_cast<SoulSize>(soulGemForm->GetMaximumCapacity())) {
                logger::error(
                    FMT_STRING(
                        "Soul gem {:08x} \"{}\" from file \"{}\" listed in group '{}' does not have a capacity matching configuration."sv),
                    form->GetFormID(),
                    form->GetName(),
                    soulGemGroup->id(),
                    soulGemId->pluginName());
                return false;
            }

            // Checks reusable soul gems for the appropriate fields.
            //
            // We use the linked soul gem field to fix a crash that occurs when
            // trying to use reusable soul gems whose base form does not have an
            // empty soul gem (the entire point of the ChargeItemFix and
            // EnchantItemFix) so it is absolutely important to get this right.
            if (soulGemForm->HasKeyword(reusableSoulGemKeyword) &&
                soulGemForm->GetContainedSoul() != RE::SOUL_LEVEL::kNone) {
                if (soulGemForm->linkedSoulGem == nullptr) {
                    logger::error(
                        FMT_STRING(
                            "Reusable soul gem {:08x} \"{}\" from file \"{}\" that contains a soul must have a linked soul gem specified in the form."sv),
                        form->GetFormID(),
                        form->GetName(),
                        soulGemGroup->id(),
                        soulGemId->pluginName());
                    return false;
                }

                if (soulGemForm->linkedSoulGem->GetContainedSoul() !=
                    RE::SOUL_LEVEL::kNone) {
                    logger::error(
                        FMT_STRING(
                            "Linked soul gem for reusable soul gem {:08x} \"{}\" from file \"{}\" is not an empty soul gem."sv),
                        form->GetFormID(),
                        form->GetName(),
                        soulGemGroup->id(),
                        soulGemId->pluginName());
                    return false;
                }
            }

            // TODO: Check NAM0 field for reusable soul gems.
            if (soulGemGroup->capacity() == SoulSize::Black) {
                switch (i) {
                case 0:
                    if (soulGemForm->GetContainedSoul() !=
                        RE::SOUL_LEVEL::kNone) {
                        logger::error(
                            FMT_STRING(
                                "Black soul gem group \"{}\" member at index {} is not an empty soul gem."sv),
                            soulGemGroup->id(),
                            i);
                        return false;
                    }
                    break;
                case 1:
                    if (soulGemForm->GetContainedSoul() !=
                        RE::SOUL_LEVEL::kGrand) {
                        logger::error(
                            FMT_STRING(
                                "Black soul gem group \"{}\" member at index {} is not a filled soul gem."sv),
                            soulGemGroup->id(),
                            i);
                        return false;
                    }
                    break;
                default:
                    logger::error(
                        FMT_STRING("Extra members found in black soul gem "
                                   "group \"{}\""),
                        soulGemGroup->id());
                    return false;
                }
            } else {
                if (static_cast<int>(soulGemForm->GetContainedSoul()) != i) {
                    logger::error(
                        FMT_STRING(
                            "Soul gem group \"{}\" member at index {} does not contain the appropriate soul size."sv),
                        soulGemGroup->id(),
                        i);
                    return false;
                }
            }

            logger::info(
                FMT_STRING("- Loaded form: {:08x} {}"sv),
                form->GetFormID(),
                form->GetName());
        }
    }

    return true;
}

bool YASTMConfig::loadConfig()
{
    namespace logger = SKSE::log;

    try {
        _readYASTMConfig();
        _readSoulGemConfigs();
        return true;
    } catch (const std::exception& error) {
        logger::error(error.what());
    }

    return false;
}

void YASTMConfig::processGameForms(RE::TESDataHandler* const dataHandler)
{
    if (_isValidConfig(dataHandler)) {
        _getGlobalForms(dataHandler);
        _createSoulGemMap(dataHandler);
    }
}

float YASTMConfig::getGlobalValue(const Key key) const
{
    namespace logger = SKSE::log;

    if (_globals.contains(key)) {
        const auto& globalId = _globals.at(key);

        if (globalId.form() == nullptr) {
            logger::info(
                FMT_STRING(
                    "Global variable '{}' ([ID:{:08x}] in file \"{}\") not yet loaded. Returning default value..."sv),
                YASTMConfig::toKeyName(key),
                globalId.formId(),
                globalId.pluginName());
            return _globalsDefaults.at(key);
        }

        return globalId.form()->value;
    }

    logger::info(
        FMT_STRING(
            "Global variable '{}' not specified in configuration. Returning default value..."sv),
        YASTMConfig::toKeyName(key));
    return _globalsDefaults.at(key);
}

bool YASTMConfig::isPartialFillsAllowed() const
{
    using namespace std::literals;

    return getGlobalValue(Key::AllowPartiallyFillingSoulGems) != 0;
}

bool YASTMConfig::isSoulDisplacementAllowed() const
{
    using namespace std::literals;

    return getGlobalValue(Key::AllowSoulDisplacement) != 0;
}

bool YASTMConfig::isSoulRelocationAllowed() const
{
    using namespace std::literals;

    return getGlobalValue(Key::AllowSoulRelocation) != 0;
}

bool YASTMConfig::isSoulShrinkingAllowed() const
{
    using namespace std::literals;

    return getGlobalValue(Key::AllowSoulShrinking) != 0;
}

RE::TESSoulGem* _getFormFromId(
    SoulGemId* const soulGemId,
    RE::TESDataHandler* const dataHandler)
{
    return dataHandler->LookupForm<RE::TESSoulGem>(
        soulGemId->formId(),
        soulGemId->pluginName());
}

void YASTMConfig::_getGlobalForms(RE::TESDataHandler* const dataHandler)
{
    using namespace std::literals;
    namespace logger = SKSE::log;

    logger::info("Loading global variable forms..."sv);

    for (auto& [key, globalId] : _globals) {
        const auto form =
            dataHandler->LookupForm(globalId.formId(), globalId.pluginName());

        if (form->Is(RE::FormType::Global)) {
            globalId.setForm(form->As<RE::TESGlobal>());

            logger::info(
                FMT_STRING("- Loaded form: {:08x} (key: {})"sv),
                form->GetFormID(),
                globalId.keyName());
        } else {
            logger::error(
                FMT_STRING(
                    "Form {:08x} \"{}\" from file \"{}\" is not a global variable."sv),
                form->GetFormID(),
                form->GetName(),
                globalId.pluginName());
        }
    }
}

void YASTMConfig::_createSoulGemMap(RE::TESDataHandler* const dataHandler)
{
    for (int i = 0; i < _whiteSoulGems.size(); ++i) {
        _whiteSoulGems[i].resize(
            getVariantCountForCapacity(static_cast<SoulSize>(i + 1)));
    }

    for (const auto& soulGemGroup : _soulGemGroups) {
        if (soulGemGroup->isReusable()) {
            if (soulGemGroup->capacity() == SoulSize::Black) {
                const auto emptySoulGemForm = _getFormFromId(
                    soulGemGroup->members()[0].get(),
                    dataHandler);
                const auto filledSoulGemForm = _getFormFromId(
                    soulGemGroup->members()[1].get(),
                    dataHandler);

                _blackSoulGemsEmpty.push_back(emptySoulGemForm);
                _blackSoulGemsFilled.push_back(filledSoulGemForm);
            } else {
                for (int i = 0; i < soulGemGroup->members().size(); ++i) {
                    const auto soulGemForm = _getFormFromId(
                        soulGemGroup->members()[i].get(),
                        dataHandler);

                    _whiteSoulGems
                        [static_cast<std::size_t>(soulGemGroup->capacity()) - 1]
                        [i]
                            .push_back(soulGemForm);
                }
            }
        }
    }

    for (const auto& soulGemGroup : _soulGemGroups) {
        if (!soulGemGroup->isReusable()) {
            if (soulGemGroup->capacity() == SoulSize::Black) {
                const auto emptySoulGemForm = _getFormFromId(
                    soulGemGroup->members()[0].get(),
                    dataHandler);
                const auto filledSoulGemForm = _getFormFromId(
                    soulGemGroup->members()[1].get(),
                    dataHandler);

                _blackSoulGemsEmpty.push_back(emptySoulGemForm);
                _blackSoulGemsFilled.push_back(filledSoulGemForm);
            } else {
                for (int i = 0; i < soulGemGroup->members().size(); ++i) {
                    const auto soulGemForm = _getFormFromId(
                        soulGemGroup->members()[i].get(),
                        dataHandler);

                    _whiteSoulGems
                        [static_cast<std::size_t>(soulGemGroup->capacity()) - 1]
                        [i]
                            .push_back(soulGemForm);
                }
            }
        }
    }

    namespace logger = SKSE::log;
    using namespace std::literals;

    const auto defaultObjectManager =
        RE::BGSDefaultObjectManager::GetSingleton();
    const RE::BGSKeyword* reusableSoulGemKeyword =
        defaultObjectManager->GetObject<RE::BGSKeyword>(
            RE::DEFAULT_OBJECT::kKeywordReusableSoulGem);

    for (int i = 0; i < _whiteSoulGems.size(); ++i) {
        const int soulCapacity = i + 1;

        for (int containedSoulSize = 0;
             containedSoulSize < _whiteSoulGems[i].size();
             ++containedSoulSize) {
            logger::info(
                FMT_STRING("Listing mapped soul gems with capacity={} "
                           "containedSoulSize={}"),
                soulCapacity,
                containedSoulSize);

            for (const auto soulGemForm :
                 _whiteSoulGems[i][containedSoulSize]) {
                logger::info(
                    FMT_STRING(
                        "- [ID:{:08x}] {} (capacity={}, containedSoulSize={}, canHoldBlackSoul={}, reusable={})"sv),
                    soulGemForm->GetFormID(),
                    soulGemForm->GetName(),
                    soulGemForm->GetMaximumCapacity(),
                    soulGemForm->GetContainedSoul(),
                    soulGemForm->HasKeyword(reusableSoulGemKeyword),
                    _canHoldBlackSoul(soulGemForm));
            }
        }
    }

    logger::info("Listing mapped empty black soul gems.");
    for (const auto soulGemForm : _blackSoulGemsEmpty) {
        logger::info(
            FMT_STRING("- [ID:{:08x}] {} (capacity={}, containedSoulSize={}, "
                       "canHoldBlackSoul={}, reusable={})"),
            soulGemForm->GetFormID(),
            soulGemForm->GetName(),
            soulGemForm->GetMaximumCapacity(),
            soulGemForm->GetContainedSoul(),
            soulGemForm->HasKeyword(reusableSoulGemKeyword),
            _canHoldBlackSoul(soulGemForm));
    }

    logger::info("Listing mapped filled black soul gems.");
    for (const auto soulGemForm : _blackSoulGemsEmpty) {
        logger::info(
            FMT_STRING(
                "- [ID:{:08x}] {} (capacity={}, containedSoulSize={}, canHoldBlackSoul={}, reusable={})"sv),
            soulGemForm->GetFormID(),
            soulGemForm->GetName(),
            soulGemForm->GetMaximumCapacity(),
            soulGemForm->GetContainedSoul(),
            soulGemForm->HasKeyword(reusableSoulGemKeyword),
            _canHoldBlackSoul(soulGemForm));
    }
}

const std::vector<RE::TESSoulGem*>& YASTMConfig::getSoulGemsWith(
    const SoulSize soulCapacity,
    const SoulSize containedSoulSize) const
{
    using namespace std::literals;

    if (!isValidSoulCapacity(soulCapacity)) {
        throw std::range_error(std::format(
            "Attempting to lookup invalid soul capacity: {}"sv,
            static_cast<int>(soulCapacity)));
    }

    if (!isValidContainedSoulSize(soulCapacity, containedSoulSize)) {
        throw std::range_error(std::format(
            "Attempting to lookup invalid contained soul size {} for capacity {}"sv,
            static_cast<int>(containedSoulSize),
            static_cast<int>(soulCapacity)));
    }

    if (soulCapacity == SoulSize::Black) {
        if (containedSoulSize == SoulSize::None) {
            return _blackSoulGemsEmpty;
        } else if (containedSoulSize == SoulSize::Black) {
            return _blackSoulGemsFilled;
        }
    } else {
        return _whiteSoulGems[static_cast<std::size_t>(soulCapacity) - 1]
                             [containedSoulSize];
    }

    throw std::range_error(std::format(
        "Attempting to lookup invalid contained soul size {} for capacity {}"sv,
        static_cast<int>(containedSoulSize),
        static_cast<int>(soulCapacity)));
}
