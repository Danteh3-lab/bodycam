// ============================================================================
// Offsets.hpp — THE single offset source for NOVA.
//
// Target : Bodycam-Win64-Shipping.exe (x64, Unreal Engine 5, LWC doubles)
// Profile: Steam app 2406770, Steam build 25228199
// Status : values marked [USED] are verified against the target build.
//          values marked [DUMP] come from an SDK dump and are validated at
//          runtime before their pointer chain is trusted.
//
// Read-only contract:
//   * The runtime (nova_core / NOVA.dll) consumes ONLY read offsets. It never
//     writes game memory, patches code, or calls engine functions.
//   * Aim/input constants are retained below under Offsets::Reference as
//     documented reference data. They are intentionally NOT consumable by
//     runtime code; the static read-only contract test rejects any reference
//     to them from core/ and dll/ sources.
// ============================================================================
#pragma once
#include <cstdint>

namespace Offsets {

	// ------------------------------------------------------------------------
	// Profile metadata — identifies the binary these offsets were verified on.
	// ------------------------------------------------------------------------
	// PE identity fields that are stable for a shipped build. A zero field is
	// "unpinned"; pinned fields are compared exactly and fail closed.
	struct ImageIdentity {
		uint32_t sizeOfImage = 0;
		uint32_t timeDateStamp = 0;
		uint32_t checkSum = 0;
		bool     valid = false;
	};

	struct Profile {
		const char* name;             // human readable profile name
		uint32_t    steamAppId;       // Steam application id
		uint64_t    steamBuild;       // Steam build id the offsets were verified on
		const char* targetProcess;    // process image name
		const char* gameModule;       // module name used for RVAs
		const char* knownFileVersion; // PE FileVersion of the target, or "" when
		                              // Steam builds do not expose one. When
		                              // non-empty the loader fails closed on a
		                              // mismatch; empty means "unknown build".
		ImageIdentity knownImage;     // pinned PE header identity of the build
	};

	// True when a profile pins at least one PE identity field.
	[[nodiscard]] constexpr bool HasPinnedImageIdentity(const Profile& profile) {
		return profile.knownImage.sizeOfImage != 0 ||
		       profile.knownImage.timeDateStamp != 0 ||
		       profile.knownImage.checkSum != 0;
	}

	// True when the measured image contradicts a pinned profile. An unpinned
	// profile never conflicts (unknown builds may inject and are gated by the
	// world invariants instead). A pinned profile whose image cannot be read
	// fails closed.
	[[nodiscard]] constexpr bool ImageIdentityConflictsWithProfile(const ImageIdentity& image,
	                                                               const Profile& profile) {
		if (!HasPinnedImageIdentity(profile)) return false;
		if (!image.valid) return true;

		if (profile.knownImage.sizeOfImage != 0 &&
		    image.sizeOfImage != profile.knownImage.sizeOfImage) {
			return true;
		}
		if (profile.knownImage.timeDateStamp != 0 &&
		    image.timeDateStamp != profile.knownImage.timeDateStamp) {
			return true;
		}
		if (profile.knownImage.checkSum != 0 && image.checkSum != profile.knownImage.checkSum) {
			return true;
		}
		return false;
	}

	inline constexpr Profile kProfileSteam25228199{
		"Bodycam Steam build 25228199",
		2406770u,
		25228199ull,
		"Bodycam-Win64-Shipping.exe",
		"Bodycam-Win64-Shipping.exe",
		"", // Steam depot builds do not ship a version resource.
		// PE identity of the verified build (measured from the installed
		// Bodycam-Win64-Shipping.exe; SizeOfImage also sanity-checks that both
		// global RVAs live inside the image).
		ImageIdentity{
			0x0A6FE000u, // SizeOfImage
			0xCF9AA4C2u, // TimeDateStamp
			0x0A2C6CC0u, // CheckSum
			true,
		},
	};

	inline constexpr Profile kActiveProfile = kProfileSteam25228199;

	inline constexpr const char* kTargetProcess = kActiveProfile.targetProcess;
	inline constexpr const char* kGameModule    = kActiveProfile.gameModule;

	// ------------------------------------------------------------------------
	// Globals — module RVAs from the SDK dump.
	// GNames and GWorld are tried first and only trusted after validation.
	// ------------------------------------------------------------------------
	namespace Globals {
		// Both RVAs were re-measured on Steam build 25228199 (2026-09-16):
		//   GNames: signature scan resolved the pool at RVA 0x099C3AC0
		//   GWorld: the stable data-section anchor slot sits at RVA 0x09C231B8;
		//           another world-like slot at 0x09C20910 was observed to go
		//           stale, which is exactly the case the scan fallback covers.
		// The scanner fallbacks remain active, so a stale hint only costs one
		// validated read attempt before the bounded scan takes over.
		constexpr uintptr_t GNames           = 0x099C3AC0; // [USED] FNamePool
		constexpr uintptr_t GWorld           = 0x09C231B8; // [USED] UWorld* slot
		constexpr int32_t   ElementsPerChunk = 0x10000;    // [DUMP] GObjects chunk size
	}

	// ------------------------------------------------------------------------
	// UE core struct layouts from the SDK dump. [DUMP] — re-verified by the
	// resolver's pointer/container validation before use.
	// ------------------------------------------------------------------------
	namespace UObject {
		constexpr uintptr_t Flags = 0x08;
		constexpr uintptr_t Index = 0x0C;
		constexpr uintptr_t Class = 0x10;
		constexpr uintptr_t Name  = 0x18;
		constexpr uintptr_t Outer = 0x20;
	}

	namespace FField {
		constexpr uintptr_t Class = 0x08;
		constexpr uintptr_t Owner = 0x10;
		constexpr uintptr_t Next  = 0x18;
		constexpr uintptr_t Name  = 0x20;
		constexpr uintptr_t Flags = 0x28;
	}

	namespace UStruct {
		constexpr uintptr_t StructBaseChain = 0x30;
		constexpr uintptr_t SuperStruct     = 0x40;
		constexpr uintptr_t Children        = 0x48;
		constexpr uintptr_t ChildProperties = 0x50;
		constexpr uintptr_t Size            = 0x58;
		constexpr uintptr_t MinAlignment    = 0x5C;
	}

	namespace UClass {
		constexpr uintptr_t CastFlags             = 0xD8;
		constexpr uintptr_t ClassDefaultObject    = 0x110;
		constexpr uintptr_t ImplementedInterfaces = 0x1D8;
	}

	namespace UEnum {
		constexpr uintptr_t Names = 0x40;
	}

	namespace UFunction {
		constexpr uintptr_t FunctionFlags = 0xB0;
		constexpr uintptr_t ExecFunction  = 0xD8;
	}

	namespace Property {
		constexpr uintptr_t ArrayDim        = 0x30;
		constexpr uintptr_t ElementSize     = 0x34;
		constexpr uintptr_t PropertyFlags   = 0x38;
		constexpr uintptr_t Offset_Internal = 0x44;
	}

	namespace InSDK {
		namespace ULevel     { constexpr uintptr_t Actors = 0xA0; }
		namespace UDataTable { constexpr uintptr_t RowMap = 0x30; }
		namespace Text {
			constexpr uintptr_t TextSize               = 0x10;
			constexpr uintptr_t TextDatOffset          = 0x00;
			constexpr uintptr_t InTextDataStringOffset = 0x18;
		}
	}

	// ------------------------------------------------------------------------
	// Gameplay offsets — [USED] read-only world model.
	// World chain: anchor slot -> UWorld -> GameInstance -> LocalPlayers[0] ->
	//              PlayerController -> CameraManager / AcknowledgedPawn
	// ------------------------------------------------------------------------

	// UWorld
	constexpr uintptr_t PersistentLevel = 0x30;
	constexpr uintptr_t GameState       = 0x160;
	constexpr uintptr_t GameInstance    = 0x1D8;

	// ULevel
	constexpr uintptr_t LevelOwningWorld = 0xC0;

	// UGameInstance
	constexpr uintptr_t LocalPlayer = 0x38; // TArray<ULocalPlayer*> (data+0, num+8)

	// ULocalPlayer
	constexpr uintptr_t LPPlayerController   = 0x30;
	constexpr uintptr_t AspectAxisConstraint = 0xB8; // uint8 EAspectAxisConstraint

	// APlayerController — read-only fields only.
	constexpr uintptr_t AcknowledgedPawn = 0x350;
	constexpr uintptr_t CameraManager    = 0x360;
	constexpr uintptr_t PlayerStateRef   = 0x2C8; // AController::PlayerState
	constexpr uintptr_t PawnController   = 0x2D8; // APawn::Controller
	constexpr uintptr_t ACPlayerState    = 0x2B0; // controller->PlayerState (alt)

	// APlayerCameraManager — two POV tiers (current + last)
	constexpr uintptr_t DefaultFOV      = 0x2C0;
	constexpr uintptr_t CameraCache     = 0x1410;
	constexpr uintptr_t POVInfo         = 0x1420; // FMinimalViewInfo
	constexpr uintptr_t CameraCacheLast = 0x1C50;
	constexpr uintptr_t POVInfoLast     = 0x1C60;

	// FMinimalViewInfo (relative to POVInfo)
	constexpr uintptr_t MviLocation    = 0x00; // FVector (3 doubles)
	constexpr uintptr_t MviRotation    = 0x18; // FRotator (3 doubles)
	constexpr uintptr_t MviFOV         = 0x30; // float
	constexpr uintptr_t MviAspectRatio = 0x5C; // float
	constexpr uintptr_t MviFlags       = 0x68; // uint32, bit0 = constrain aspect

	// AGameStateBase
	constexpr uintptr_t PlayerArray       = 0x2C0; // TArray<APlayerState*>
	constexpr uintptr_t PlayerArrayData   = 0x00;
	constexpr uintptr_t PlayerArrayNum    = 0x08;
	constexpr uintptr_t PlayerArrayStride = 0x08;

	// APlayerState
	constexpr uintptr_t PSPawn   = 0x320;
	constexpr uintptr_t PSName   = 0x340; // FString (TArray<wchar>): data+0, num+8
	constexpr uintptr_t PSTeamId = 0x388; // int
	constexpr uintptr_t PSKills  = 0x38C; // int
	constexpr uintptr_t PSDeaths = 0x390; // int

	// APawn / ACharacter
	constexpr uintptr_t RootComponent         = 0x1B8;
	constexpr uintptr_t SkeletalMeshComponent = 0x328;
	constexpr uintptr_t CharacterMovement     = 0x330;
	constexpr uintptr_t CapsuleComponent      = 0x338;

	// USceneComponent
	constexpr uintptr_t RelativeLocation = 0x128;
	constexpr uintptr_t RelativeRotation = 0x140;
	constexpr uintptr_t Velocity         = 0x170;
	constexpr uintptr_t ComponentToWorld = 0x1D0; // FTransform (0x60)
	constexpr uintptr_t C2WTranslation   = 0x1F0; // FVector inside ComponentToWorld
	constexpr uintptr_t C2WScale         = 0x210;

	// UCapsuleComponent
	constexpr uintptr_t CapsuleHalfHeight = 0x508; // float pair with radius
	constexpr uintptr_t CapsuleRadius     = 0x50C;

	// USkinnedMeshComponent (pose)
	constexpr uintptr_t SkeletalMeshAssetOld = 0x520; // fallback asset pointer
	constexpr uintptr_t SkinnedAsset         = 0x528; // USkeletalMesh
	constexpr uintptr_t LeaderPoseComponent  = 0x530;
	constexpr uintptr_t ActiveBoneArray      = 0x598; // TArray<FTransform>[2], stride 0x10
	constexpr uintptr_t BoneArrayStride      = 0x10;
	constexpr uintptr_t BoneEditIndex        = 0x5DC;
	constexpr uintptr_t BoneBufferIndex      = 0x5E0; // int, active buffer 0/1
	constexpr uintptr_t LeaderBoneMap        = 0x608;
	constexpr uintptr_t BoneStride           = 0x60;  // sizeof(FTransform)

	// FTransform (UE5 LWC, 0x60 bytes, doubles)
	constexpr uintptr_t FTQuat        = 0x00; // fquat, 4 doubles
	constexpr uintptr_t FTTranslation = 0x20; // FVector, 3 doubles
	constexpr uintptr_t FTScale       = 0x40; // FVector, 3 doubles

	// Bodycam pawn extensions
	constexpr uintptr_t BCAbilitySystem = 0x658;
	constexpr uintptr_t BCPawnExt       = 0x660;
	constexpr uintptr_t BCCharacterSet  = 0x668; // attribute set container

	// Attribute entries (relative to character set)
	constexpr uintptr_t AttrHealth         = 0x88;
	constexpr uintptr_t AttrMaxHealth      = 0x98;
	constexpr uintptr_t AttrGadgetCooldown = 0xA8;
	constexpr uintptr_t AttrHealing        = 0xB8;
	constexpr uintptr_t AttrDamage         = 0xC8;
	constexpr uintptr_t AttrBaseValue      = 0x08; // float inside entry
	constexpr uintptr_t AttrCurrentValue   = 0x0C; // float inside entry

	// Direct health floats (relative to character set)
	constexpr uintptr_t HealthCurrent    = 0x94;
	constexpr uintptr_t MaxHealthCurrent = 0xA4;

	// USkeletalMesh RefSkeleton scan
	constexpr uintptr_t RefSkelRawBoneInfo   = 0x00; // TArray<FBoneInfo>
	constexpr uintptr_t RefSkelRawBonePose   = 0x10; // TArray<FTransform>
	constexpr uintptr_t RefSkelFinalBoneInfo = 0x20;
	constexpr uintptr_t RefSkelFinalBonePose = 0x30;
	constexpr uintptr_t MeshBoneInfoStride   = 0x0C; // FName(0x00) + parent int32(0x08)
	constexpr uintptr_t MeshBoneInfoParent   = 0x08;
	constexpr uintptr_t RefSkelScanMax       = 0x520; // scan asset+0..0x520 step 8

	// ------------------------------------------------------------------------
	// FNamePool layout — [USED].
	// pool+0x10 = block pointer array; entry = block[idx>>16] + (idx&0xFFFF)*2;
	// header u16 at entry+0: bit0 = wide string, length = header >> 6,
	// characters start at entry+2.
	// ------------------------------------------------------------------------
	namespace NamePool {
		constexpr uintptr_t BlocksOffset  = 0x10;
		constexpr int       MaxBlockIndex = 8192;
		constexpr int       MaxNameChars  = 250;
	}

	// ------------------------------------------------------------------------
	// Signatures — patch-surviving fallbacks. Static RVAs go stale every
	// update; these patterns re-resolve them at runtime with bounded scans.
	// '?' = wildcard.
	// ------------------------------------------------------------------------
	namespace Signatures {

		// FNamePool reference: 33 bytes. Two RIP-relative LEAs (at +4 and +13)
		// must resolve to the SAME address; that address is the pool.
		//   t1 = va + i + 9  + rel32(+5)
		//   t2 = va + i + 18 + rel32(+14); require t1 == t2, then plausibility
		//   check (entry 0 decodes, "None" == certain).
		inline constexpr char FNamePoolPattern[] =
			"\x74\x09\x4C\x8D\x05????\xEB\x16\x48\x8D\x0D????\xE8????"
			"\x4C\x8B\xC0\xC6\x05????\x01";
		inline constexpr char FNamePoolMask[] =
			"xxxxx????xxxxxx????x????xxxxxx????x"; // 33 chars
		constexpr int FNamePoolRel1 = 5;   // rel32 offset of first LEA
		constexpr int FNamePoolRel2 = 14;  // rel32 offset of second LEA
		constexpr int FNamePoolLen  = 33;
	}

	// ------------------------------------------------------------------------
	// UE standard container layouts assumed by the code (stable across builds)
	// ------------------------------------------------------------------------
	namespace Std {
		constexpr uintptr_t TArrayData = 0x00;
		constexpr uintptr_t TArrayNum  = 0x08;
		constexpr uintptr_t TArrayMax  = 0x0C;
	}

	// ------------------------------------------------------------------------
	// Validation / tuning constants used alongside the offsets
	// ------------------------------------------------------------------------
	namespace Limits {
		constexpr int   MaxPlayers   = 128;
		constexpr int   MaxBones     = 1024;
		constexpr int   MaxNameLen   = 64;
		constexpr int   MaxParents   = 256;
		constexpr int   MaxLocalPlayers = 8;
		constexpr float MinHealth    = 0.0f;
		constexpr float MaxHealth    = 100000.0f;
		constexpr float FovMin       = 20.0f;
		constexpr float FovMax       = 170.0f;
		constexpr float FallbackFOV  = 90.0f;
		constexpr float FovScale     = 1.150f; // calibrated projection correction
	}

	namespace Scan {
		constexpr unsigned WorldScanBudgetMs = 1000;
		constexpr size_t   ChunkBytes        = 0x10000;
		constexpr size_t   ChunkOverlap      = 0x40;
		constexpr int      RecoverLimitMin   = 120;
		constexpr int      RecoverLimitMax   = 7680;
	}

	namespace Keys {
		constexpr int MenuToggle = 0x2D; // VK_INSERT
		constexpr int Unload     = 0x2E; // VK_DELETE
	}

	// ========================================================================
	// REFERENCE DATA — NOT CONSUMED BY RUNTIME CODE.
	//
	// These aim/input constants are retained only so the SDK layout remains
	// documented in one place. The read-only contract test rejects any
	// reference to this namespace from core/ and dll/ translation units.
	// ========================================================================
	namespace Reference {

		// APlayerController aim/input fields.
		constexpr uintptr_t RemoteViewPitch       = 0x2BA;
		constexpr uintptr_t ControlRotation       = 0x320; // FRotator of doubles
		constexpr uintptr_t PCTargetViewRotation  = 0x378;
		constexpr uintptr_t PCPlayerInput         = 0x420;
		constexpr uintptr_t RotationInput         = 0x528; // doubles
		constexpr uintptr_t RotationInputPitch    = 0x528;
		constexpr uintptr_t RotationInputYaw      = 0x530;
		constexpr uintptr_t RotationInputRoll     = 0x538;
		constexpr uintptr_t InputYawScale         = 0x540;
		constexpr uintptr_t InputPitchScale       = 0x544;
		constexpr uintptr_t InputRollScale        = 0x548;
		constexpr int       AimDefaultKey         = 0x02; // VK_RBUTTON

		// Engine function RVAs — [USED] by the legacy build's aim assist only.
		namespace Calls {
			constexpr uintptr_t AddPitchInput = 0x3CB83C0;
			constexpr uintptr_t AddRollInput  = 0x3CB8450;
			constexpr uintptr_t AddYawInput   = 0x3CB85D0;
			constexpr uintptr_t ProcessEvent  = 0x014AB3A0; // [DUMP]
			constexpr uint8_t   ProcessEventIdx = 0x4F;     // [DUMP] vtable index
			constexpr uintptr_t AppendString    = 0x0127FFB0; // [DUMP]
		}

		// Signature data for the legacy input functions (documented for
		// completeness; unreachable from runtime code).
		inline constexpr unsigned char AddInputPrologue[12] = {
			0x40, 0x53, 0x48, 0x83, 0xEC, 0x30,
			0x48, 0x8B, 0x01, 0x48, 0x8B, 0xD9
		};
		constexpr int AddInputFnSize  = 0x8C;
		constexpr int AddInputTailOff = 0x73;
		constexpr int AddInputTailLen = 25;
	}

} // namespace Offsets
