#pragma once
#include <array>
#include <cstdint>
namespace weapon_inspect
{
	constexpr int inspectState = 78;
	constexpr unsigned inspectAction = 86;
	constexpr unsigned inspectAnimation = 433;

	// Values read by the native controller, independent of either executable's
	// player-state layout. The Replay binding must admit the root before calling
	// this policy and provide the native movement, timing and cancellation calls.
	// No hook currently invokes this component.
	struct Hand
	{
		int state{}, time{}, queued{}, oldTime{}, oldState{};
		unsigned previousAnimation{}, animation{};
	};

	struct Input
	{
		uint64_t buttons{}, oldButtons{};
		uint32_t movementFlags{}, weaponFlags{}, weaponFlagsHi{};
		int leftMode{}, lastHand{}, pmType{};
		bool enabled{}, queueEnabled{}, gestureEnabled{}, gestureInput{};
		std::array<Hand, 2> hands{};
	};

	inline void SetAnimation(Hand& hand, unsigned action) noexcept
	{
		const auto previous = hand.previousAnimation, current = hand.animation;
		hand.previousAnimation = current;
		const auto base = ((previous & ~128u) == action && (current & ~128u) != action) ? previous : current;
		hand.animation = ((~base) & 128u) | action;
	}

	// GenericReset must refresh the hand snapshot after calling the native reset.
	// RecordStopTime owns the later-build timestamp; it must not write Replay's
	// unrelated cooldown at playerState+10CC.
	template <class Operations>
	void StopHands(Input& input, Operations& native, int commandTime)
	{
		if (input.lastHand < -1 || input.lastHand > 1) return;
		for (int i = 0; i <= input.lastHand; ++i)
		{
			auto& hand = input.hands[i];
			if (hand.queued == 6) hand.queued = 0;
			if (hand.state != inspectState) continue;
			native.GenericReset(i, hand);
			if (input.pmType < 7) SetAnimation(hand, 1);
		}
		native.RecordStopTime(commandTime);
	}

	struct Movement
	{
		int state{}, blockingField{}, commandTime{}, serverTime{};
		uint32_t movementFlags{}, otherFlags{}, otherMask{}, weaponFlags{};
	};

	template <class Operations>
	bool MovementAllowed(const Movement& input, Operations& native)
	{
		if (native.Blocked()) return false;
		if (uint32_t(input.state) - 26u <= 6u || (input.movementFlags & (1u << 5)) || uint32_t(input.state) - 40u <= 3u ||
			(input.movementFlags & (1u << 6)) || (input.otherFlags & (input.otherMask << 22)) || (input.weaponFlags & (1u << 26)))
			return false;
		if (!native.Valid()) return false;
		if (native.Sprinting() && !native.SprintAllowed(input.commandTime)) return false;
		if (native.TacticalSprinting() && !native.TacticalSprintAllowed(input.commandTime)) return false;
		if (input.blockingField || native.WeaponBlocked() || !native.WeaponReady(input.serverTime)) return false;
		return !(input.movementFlags & (1u << 28));
	}

	template <class Operations>
	void Process(Input& input, Operations& native)
	{
		if (!input.enabled || input.lastHand < -1 || input.lastHand > 1) return;
		const auto pressed = input.buttons & ~input.oldButtons;
		if (input.hands[0].state == inspectState)
		{
			if (pressed & 0x200205)
			{
				for (int i = 0; i <= input.lastHand; ++i)
				{
					const auto& hand = input.hands[i];
					const int elapsed = hand.oldState == hand.state ? hand.oldTime : 0;
					if (native.CancelTest(i, elapsed)) native.CancelHand(i);
				}
				native.Reset();
				return;
			}
			if (input.gestureInput && native.Gestures() && input.gestureEnabled) native.Gesture();
		}
		if (!(pressed & (uint64_t(1) << 44)) && input.hands[0].queued != 6) return;
		if (input.hands[0].queued == 6) input.hands[0].queued = 0;
		const bool alternate = (input.weaponFlags & ((1u << 17) | (1u << 27))) && !(input.weaponFlagsHi & 4);
		const bool permissionAlternate = native.SpecialAlternate() ? (input.weaponFlags & (1u << 29)) != 0 : alternate;
		if (!native.Permission(permissionAlternate) || !native.MovementAllowed() || (input.movementFlags & (1u << 20))) return;
		for (int i = 0; i <= input.lastHand; ++i)
		{
			const int state = input.hands[i].state;
			if (state == inspectState)
			{
				if (input.queueEnabled) input.hands[0].queued = 6;
				return;
			}
			const bool directlyAllowed = state >= 0 && state <= 59 && ((uint64_t(0xC01000000000001) >> state) & 1);
			if (!directlyAllowed && !native.Interruptible(i)) return;
		}
		for (int i = 0; i <= input.lastHand; ++i)
		{
			const bool left = !native.LeftBlocked() && native.LeftSupported() && input.leftMode == 1;
			const auto duration = native.Duration(alternate, left);
			if (duration == 65535) continue;
			if (native.Gestures()) native.Gesture();
			auto& hand = input.hands[i];
			if (hand.state == 16) native.Event(38);
			hand.queued = 0;
			if (input.pmType < 7) SetAnimation(hand, inspectAction);
			hand.time = duration;
			hand.state = inspectState;
		}
	}
}
