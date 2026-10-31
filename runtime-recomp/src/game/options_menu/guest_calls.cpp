#include "guest_calls.hpp"

#include "accessibility/guest_text.hpp"

namespace dkr::options_menu {
namespace {

constexpr std::uint32_t kFrameBytes = 0x400U;
// Scratch for one string at a time, past the outgoing argument area.
constexpr std::uint32_t kScratchOffset = 0x100U;
constexpr std::uint32_t kScratchBytes = 0x200U;

gpr guest_register(std::uint32_t value) {
    return static_cast<gpr>(static_cast<std::int32_t>(value));
}

} // namespace

GuestCalls::GuestCalls(std::uint8_t* rdram, const recomp_context& context)
    : rdram_(rdram), saved_(context) {
    const auto stack = static_cast<std::uint32_t>(context.r29);
    if (rdram == nullptr || stack % 8U != 0U || stack < accessibility::kGuestBegin + kFrameBytes ||
        !accessibility::guest_readable(stack - kFrameBytes, kFrameBytes)) {
        return;
    }
    frame_ = stack - kFrameBytes;
    valid_ = true;
}

std::uint32_t GuestCalls::call(runtime::RecompiledEntrypoint function,
                               std::initializer_list<std::uint32_t> arguments) {
    if (!valid_ || function == nullptr || arguments.size() > 8U) {
        return 0U;
    }
    recomp_context context = saved_;
    context.r29 = guest_register(frame_);
    context.f_odd = context.mips3_float_mode ? &context.f1.u32l : &context.f0.u32h;
    std::uint8_t* rdram = rdram_;
    unsigned index = 0;
    for (const std::uint32_t argument : arguments) {
        switch (index) {
            case 0: context.r4 = guest_register(argument); break;
            case 1: context.r5 = guest_register(argument); break;
            case 2: context.r6 = guest_register(argument); break;
            case 3: context.r7 = guest_register(argument); break;
            default: MEM_W(0, guest_register(frame_ + index * 4U)) = static_cast<std::int32_t>(argument); break;
        }
        ++index;
    }
    function(rdram_, &context);
    return static_cast<std::uint32_t>(context.r2);
}

std::optional<std::uint32_t> GuestCalls::text(std::string_view value) {
    if (!valid_ || value.size() >= kScratchBytes) {
        return std::nullopt;
    }
    std::uint8_t* rdram = rdram_;
    const std::uint32_t address = frame_ + kScratchOffset;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto byte = static_cast<unsigned char>(value[i]);
        MEM_B(static_cast<std::int32_t>(i), guest_register(address)) =
            static_cast<std::int8_t>(byte >= 0x20U && byte <= 0x7EU ? byte : '?');
    }
    MEM_B(static_cast<std::int32_t>(value.size()), guest_register(address)) = 0;
    return address;
}

} // namespace dkr::options_menu
