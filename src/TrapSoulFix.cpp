#include "TrapSoulFix.hpp"

#include <mutex>
#include <queue>

#include <xbyak/xbyak.h>

#include <REL/Relocation.h>
#include <SKSE/SKSE.h>

#include <RE/A/Actor.h>
#include <RE/B/BGSDefaultObjectManager.h>
#include <RE/B/BGSKeyword.h>
#include <RE/M/Misc.h>
#include <RE/S/SoulLevels.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESSoulGem.h>

#include "Victim.hpp"
#include "config/YASTMConfig.hpp"
#include "messages.hpp"

namespace native {
    /**
     * @brief Returns a pointer to the "stat manager".
     * (Yes, I have no idea exactly what this is, besides knowing what happens when it's removed).
     */
    void* getStatManager()
    {
        using func_t = decltype(getStatManager);
        REL::Relocation<func_t> func{
            REL::ID{37916}}; // SkyrimSE.exe + 0x636c40 (v1.5.97.0)
        return func();
    }

    /**
     * @brief Increments the Souls Trapped stat given the manager and the victim.
     * (Yes, I have no idea exactly what this is, besides knowing what happens when it's removed).
     */
    int incrementStat(void* manager, RE::Actor* const* const victim)
    {
        using func_t = decltype(incrementStat);
        REL::Relocation<func_t> func{
            REL::ID{37912}}; // SkyrimSE.exe + 0x6363e0 (v1.5.97.0)
        return func(manager, victim);
    }

    /**
     * @brief Checks the soul trap status of the given actor.
     *
     * Note that this function returns weird numbers I can't figure out the significance of.
     * It's not important since all we need this for is check if it's 0 or not, but it would
     * be nice to know regardless.
     *  
     * @return 0 if it has been previously soul trapped. Weird numbers if not.
     */
    std::uint64_t soulTrapVictimStatus(RE::Actor* const actor)
    {
        using func_t = decltype(soulTrapVictimStatus);
        REL::Relocation<func_t> func{
            REL::ID{37861}}; // SkyrimSE.exe + 0x634830 (v1.5.97.0)
        return func(actor);
    }
}

void _incrementSoulsTrappedStat(
    RE::Actor* const caster,
    RE::Actor* const victim)
{
    if (caster == nullptr || victim == nullptr || !caster->IsPlayerRef()) {
        return;
    }

    void* manager = native::getStatManager();
    native::incrementStat(manager, &victim);
}

void _debugNotification(const char* message, RE::Actor* const caster)
{
    if (caster == nullptr || !caster->IsPlayerRef()) {
        return;
    }

    RE::DebugNotification(message);
}

void _debugNotification(
    const char* message,
    RE::Actor* const caster,
    const Victim& victim)
{
    if (caster == nullptr || victim.isDisplacedSoul() ||
        !caster->IsPlayerRef()) {
        return;
    }

    RE::DebugNotification(message);
}

int _indexOfFirstOwnedObjectInList(
    const RE::TESObjectREFR::InventoryCountMap& inventoryCountMap,
    const std::vector<RE::TESSoulGem*>& objectsToSearch)
{
    for (int i = 0; i < objectsToSearch.size(); ++i) {
        const auto boundObject = objectsToSearch[i]->As<RE::TESBoundObject>();

        if (inventoryCountMap.contains(boundObject) &&
            inventoryCountMap.at(boundObject) > 0) {
            return i;
        }
    }

    return -1;
}

void _replaceItem(
    RE::Actor* actor,
    RE::TESBoundObject* const objectToAdd,
    RE::TESBoundObject* const objectToRemove)
{
    actor->AddObjectToContainer(objectToAdd, nullptr, 1, nullptr);
    actor->RemoveItem(
        objectToRemove,
        1,
        RE::ITEM_REMOVE_REASON::kRemove,
        nullptr,
        nullptr);
}

std::tuple<RE::TESSoulGem*, RE::TESSoulGem*> getSoulGemsAtIndex(
    const std::size_t index,
    const std::vector<RE::TESSoulGem*>& sourceGems,
    const std::vector<RE::TESSoulGem*>& targetGems)
{
    return std::make_tuple(sourceGems[index], targetGems[index]);
}

bool _fillSoulGem(
    RE::Actor* const caster,
    const SoulSize capacity,
    const SoulSize sourceContainedSoulSize,
    const SoulSize targetContainedSoulSize,
    const RE::TESObjectREFR::InventoryCountMap& soulGemCountMap);
bool _fillSoulGem(
    RE::Actor* const caster,
    const std::vector<RE::TESSoulGem*>& sourceSoulGems,
    const std::vector<RE::TESSoulGem*>& targetSoulGems,
    const RE::TESObjectREFR::InventoryCountMap& soulGemCountMap);

bool _fillSoulGem(
    RE::Actor* const caster,
    const SoulSize capacity,
    const SoulSize sourceContainedSoulSize,
    const SoulSize targetContainedSoulSize,
    const RE::TESObjectREFR::InventoryCountMap& soulGemCountMap)
{
    const YASTMConfig& config = YASTMConfig::getInstance();

    const auto& sourceSoulGems =
        config.getSoulGemsWith(capacity, sourceContainedSoulSize);
    const auto& targetSoulGems =
        config.getSoulGemsWith(capacity, targetContainedSoulSize);

    return _fillSoulGem(
        caster,
        sourceSoulGems,
        targetSoulGems,
        soulGemCountMap);
}

bool _fillSoulGem(
    RE::Actor* const caster,
    const std::vector<RE::TESSoulGem*>& sourceSoulGems,
    const std::vector<RE::TESSoulGem*>& targetSoulGems,
    const RE::TESObjectREFR::InventoryCountMap& soulGemCountMap)
{
    const int firstOwnedIndex =
        _indexOfFirstOwnedObjectInList(soulGemCountMap, sourceSoulGems);

    if (firstOwnedIndex >= 0) {
        const auto soulGemToAdd = targetSoulGems[firstOwnedIndex];
        const auto soulGemToRemove = sourceSoulGems[firstOwnedIndex];

        _replaceItem(caster, soulGemToAdd, soulGemToRemove);

        return true;
    }

    return false;
}

bool _trapBlackSoul(
    RE::Actor* const caster,
    const Victim& victim,
    const RE::TESObjectREFR::InventoryCountMap& soulGemCountMap)
{
    using namespace std::literals;
    namespace logger = SKSE::log;

    const bool isSoulTrapped = _fillSoulGem(
        caster,
        victim.soulSize(),
        SoulSize::None,
        victim.soulSize(),
        soulGemCountMap);

    if (isSoulTrapped) {
        _debugNotification(getMessage(Message::SoulCaptured), caster, victim);
        _incrementSoulsTrappedStat(caster, victim.actor());

        return true;
    }

    return false;
}

bool _trapFullSoul(
    RE::Actor* const caster,
    const Victim& victim,
    const bool allowPartial,
    const bool allowDisplacement,
    const bool allowRelocation,
    std::priority_queue<Victim, std::vector<Victim>, std::greater<Victim>>&
        victims,
    const RE::TESObjectREFR::InventoryCountMap& soulGemCountMap)
{
    const YASTMConfig& config = YASTMConfig::getInstance();

    if (allowRelocation) {
        // If relocation is allowed, we prioritize the soul gem capacity over the size of the displaced soul.
        const SoulSize maxSoulCapacityToSearch =
            allowPartial ? SoulSize::Grand : victim.soulSize();
        const SoulSize maxContainedSoulSizeToSearch =
            allowDisplacement ? victim.soulSize() : SoulSize::Petty;

        for (std::size_t soulCapacity = victim.soulSize();
             soulCapacity <= maxSoulCapacityToSearch;
             ++soulCapacity) {
            const auto& targetSoulGems = config.getSoulGemsWith(
                toSoulSize(soulCapacity),
                victim.soulSize());

            for (std::size_t containedSoulSize = SoulSize::None;
                 containedSoulSize < maxContainedSoulSizeToSearch;
                 ++containedSoulSize) {
                const auto& sourceSoulGems = config.getSoulGemsWith(
                    toSoulSize(soulCapacity),
                    toSoulSize(containedSoulSize));

                const bool result = _fillSoulGem(
                    caster,
                    sourceSoulGems,
                    targetSoulGems,
                    soulGemCountMap);

                if (result) {
                    if (allowRelocation && containedSoulSize > SoulSize::None) {
                        _debugNotification(
                            getMessage(Message::SoulDisplaced),
                            caster,
                            victim);
                        victims.emplace(toSoulSize(containedSoulSize));
                    } else {
                        _debugNotification(
                            getMessage(Message::SoulCaptured),
                            caster,
                            victim);
                    }

                    _incrementSoulsTrappedStat(caster, victim.actor());

                    return true;
                }
            }
        }
    } else {
        // If relocation is disabled, we need to displace the smallest soul first to reduce soul loss.
        const SoulSize maxSoulCapacityToSearch =
            allowPartial ? SoulSize::Grand : victim.soulSize();
        const SoulSize maxContainedSoulSizeToSearch =
            allowDisplacement ? victim.soulSize() : SoulSize::Petty;

        for (std::size_t containedSoulSize = SoulSize::None;
             containedSoulSize < maxContainedSoulSizeToSearch;
             ++containedSoulSize) {
            for (std::size_t soulCapacity = victim.soulSize();
                 soulCapacity <= maxSoulCapacityToSearch;
                 ++soulCapacity) {
                const bool result = _fillSoulGem(
                    caster,
                    toSoulSize(soulCapacity),
                    toSoulSize(containedSoulSize),
                    victim.soulSize(),
                    soulGemCountMap);

                if (result) {
                    if (allowRelocation && containedSoulSize > SoulSize::None) {
                        _debugNotification(
                            getMessage(Message::SoulDisplaced),
                            caster,
                            victim);
                        victims.emplace(toSoulSize(containedSoulSize));
                    } else {
                        _debugNotification(
                            getMessage(Message::SoulCaptured),
                            caster,
                            victim);
                    }

                    _incrementSoulsTrappedStat(caster, victim.actor());

                    return true;
                }
            }
        }
    }

    return false;
}

bool _trapShrunkSoul(
    RE::Actor* const caster,
    const Victim& victim,
    const bool allowDisplacement,
    const RE::TESObjectREFR::InventoryCountMap& soulGemCountMap,
    std::priority_queue<Victim, std::vector<Victim>, std::greater<Victim>>&
        victims)
{
    const YASTMConfig& config = YASTMConfig::getInstance();

    for (int soulCapacity = victim.soulSize() - 1;
         soulCapacity > SoulSize::None;
         --soulCapacity) {
        const auto& targetSoulGems = config.getSoulGemsWith(
            toSoulSize(soulCapacity),
            toSoulSize(soulCapacity));

        // We actually search up to only 'max - 1' but whatever.
        const SoulSize maxContainedSoulSizeToSearch =
            allowDisplacement ? victim.soulSize() : SoulSize::Petty;

        for (int containedSoulSize = SoulSize::None;
             containedSoulSize < maxContainedSoulSizeToSearch;
             ++containedSoulSize) {
            const auto& sourceSoulGems = config.getSoulGemsWith(
                toSoulSize(soulCapacity),
                toSoulSize(containedSoulSize));

            const bool isFillSuccessful = _fillSoulGem(
                caster,
                sourceSoulGems,
                targetSoulGems,
                soulGemCountMap);

            if (isFillSuccessful) {
                _debugNotification(
                    getMessage(Message::SoulShrunk),
                    caster,
                    victim);
                _incrementSoulsTrappedStat(caster, victim.actor());

                if (containedSoulSize > SoulSize::None) {
                    victims.emplace(toSoulSize(containedSoulSize));
                }

                return true;
            }
        }
    }

    return false;
}

std::mutex _trapSoulMutex;

bool trapSoul(RE::Actor* const caster, RE::Actor* const victimActor)
{
    using namespace std::literals;
    namespace logger = SKSE::log;

    logger::trace("entering YASTM trap soul function"sv);

    std::lock_guard<std::mutex> guard{_trapSoulMutex};

    if (caster == nullptr || victimActor == nullptr || caster->IsDead(false) ||
        !victimActor->IsDead(false) ||
        native::soulTrapVictimStatus(victimActor) == 0) {
        logger::trace("Exiting YASTM trap soul function"sv);
        return false;
    }

    bool isSoulTrapSuccessful = false;

    try {
        const YASTMConfig& config = YASTMConfig::getInstance();
        std::priority_queue<Victim, std::vector<Victim>, std::greater<Victim>>
            victims; // We need this to handle displaced souls.

        victims.emplace(victimActor);

        // Snapshot the configuration here so it will be immune to external changes for this run.
        // (And also because it's shorter.)
        const bool allowPartial = config.isPartialFillsAllowed();
        const bool allowDisplacement = config.isSoulDisplacementAllowed();
        const bool allowRelocation = config.isSoulRelocationAllowed();
        const bool allowShrinking = config.isSoulShrinkingAllowed();

        logger::trace("Found configuration:"sv);
        logger::trace("- Allow partial: {}"sv, allowPartial);
        logger::trace("- Allow displacement: {}"sv, allowDisplacement);
        logger::trace("- Allow relocation: {}"sv, allowRelocation);
        logger::trace("- Allow shrinking: {}"sv, allowShrinking);

        while (!victims.empty()) {
            const Victim victim = victims.top();
            victims.pop();

            auto soulGemCountMap =
                caster->GetInventoryCounts([](RE::TESBoundObject& boundObject) {
                    return boundObject.IsSoulGem();
                });

            // Black souls are simple since they're all or none. Either you have a black soul gem or you don't.
            // Nothing fancy to account for.
            if (victim.soulSize() == SoulSize::Black) {
                if (_trapBlackSoul(caster, victim, soulGemCountMap)) {
                    isSoulTrapSuccessful = true;
                    continue; // Process next soul.
                }
            } else /* White souls */ {
                if (_trapFullSoul(
                        caster,
                        victim,
                        allowPartial,
                        allowDisplacement,
                        allowRelocation,
                        victims,
                        soulGemCountMap)) {
                    isSoulTrapSuccessful = true;
                    continue; // Process next soul.
                }

                // If we failed the previous step, start shrinking.
                if (allowShrinking) {
                    if (_trapShrunkSoul(
                            caster,
                            victim,
                            allowDisplacement,
                            soulGemCountMap,
                            victims)) {
                        isSoulTrapSuccessful = true;

                        continue; // Process next soul.
                    }
                }
            }
        }

        if (isSoulTrapSuccessful) {
            // Flag the victim so we don't soul trap the same one multiple times.
            if (RE::AIProcess* const process = victimActor->currentProcess;
                process) {
                if (process->middleHigh) {
                    process->middleHigh->unk325 = true;
                }
            }
        } else {
            _debugNotification(
                getMessage(Message::NoSoulGemLargeEnough),
                caster);
        }

        return isSoulTrapSuccessful;
    } catch (const std::exception& error) {
        logger::error(error.what());
    }

    logger::trace("Exiting YASTM trap soul function"sv);

    return false;
}

void _handleMessage(SKSE::MessagingInterface::Message* message)
{
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        YASTMConfig::getInstance().processGameForms(
            RE::TESDataHandler::GetSingleton());
    }
}

/**
 * Check if memory has the expected bytes for patching.
 */
bool _isTrapSoulPatchable(
    const std::uintptr_t baseAddress,
    const std::uintptr_t callOffset,
    const std::uintptr_t returnOffset)
{
    namespace logger = SKSE::log;

    const std::uint8_t expectedEntry[] = {
        // clang-format off
        // .text:000000014063491F
        0x48, 0x8b, 0xf2,                  // mov rsi, rdx
        0x4c, 0x8b, 0xf1,                  // mov r14, rcx
        0x40, 0x32, 0xff,                  // xor dil, dil
        0x48, 0x8b, 0x01,                  // mov rax, [rcx]
        0x33, 0xd2,                        // xor edx, edx
        0xff, 0x90, 0xc8, 0x04, 0x00, 0x00 // call    qword ptr [rax+4C8h]
        // clang-format on
    };

    const std::uint8_t expectedExit[] = {
        // clang-format off
        // .text:0000000140634B56
        0x4c, 0x8d, 0x5c, 0x24, 0x70, // lea     r11, [rsp+98h+var_28]
        0x49, 0x8b, 0x5b, 0x38,       // mov     rbx, [r11+38h]
        0x49, 0x8b, 0x6b, 0x40,       // mov     rbp, [r11+40h]
        0x49, 0x8b, 0xe3,             // mov     rsp, r11
        // clang-format on
    };

    if (std::memcmp(
            reinterpret_cast<std::uint8_t*>(
                static_cast<std::uintptr_t>(baseAddress + callOffset)),
            expectedEntry,
            sizeof expectedEntry) != 0) {
        logger::critical(
            "[TRAPSOUL] Expected bytes for soul trap handling at call offset not found."sv);
        return false;
    }

    if (std::memcmp(
            reinterpret_cast<std::uint8_t*>(
                static_cast<std::uintptr_t>(baseAddress + returnOffset)),
            expectedExit,
            sizeof expectedExit) != 0) {
        logger::critical(
            "[TRAPSOUL] Expected bytes for soul trap handling at return offset not found."sv);
        return false;
    }

    return true;
}

bool installTrapSoulFix()
{
    using namespace std::literals;
    namespace logger = SKSE::log;

    YASTMConfig::getInstance().loadConfig();

    auto messaging = SKSE::GetMessagingInterface();
    messaging->RegisterListener(_handleMessage);

    const REL::ID soulTrap1_id{37863};
    constexpr std::uintptr_t callOffset = 0x1f;
    constexpr std::uintptr_t returnOffset = 0x256;

    if (!_isTrapSoulPatchable(
            soulTrap1_id.address(),
            callOffset,
            returnOffset)) {
        return false;
    }

    // This simply sets up the registers so they will be passed to our
    // replacement function correctly, and jumps back to the our original
    // function's ending address.
    struct TrapSoulCall : Xbyak::CodeGenerator {
        explicit TrapSoulCall(
            const REL::ID& soulTrap1_id,
            const std::uintptr_t returnOffset)
        {
            Xbyak::Label trapSoulLabel;
            Xbyak::Label returnLabel;

            mov(rdx, r9); // victim
            mov(rcx, r8); // caster

            call(ptr[rip + trapSoulLabel]);

            jmp(ptr[rip + returnLabel]);

            L(trapSoulLabel);
            dq(reinterpret_cast<std::uint64_t>(trapSoul));

            L(returnLabel);
            dq(soulTrap1_id.address() + returnOffset);
        }
    };

    TrapSoulCall patch{soulTrap1_id, returnOffset};
    patch.ready();

    logger::info(FMT_STRING("[CHARGE] Patch size: {}"sv), patch.getSize());

    auto& trampoline = SKSE::GetTrampoline();
    trampoline.write_branch<5>(
        soulTrap1_id.address() + callOffset,
        trampoline.allocate(patch));

    return true;
}
