#include "SadMemEdit.h"
#include "DisruptHook.h"
#include "CameraSystem.h"

void CameraSystem::Initialize() {
	bFreeCamMode = false; //Freecam should be off by default.
	DWORD WINAPI CameraSystemThread(LPVOID lpParam);
	CreateThread(0, 0, CameraSystemThread, GetModuleHandleA("Disrupt_b64.dll"), 0, 0);
}

DWORD WINAPI CameraSystemThread(LPVOID lpParam)
{
	CameraSystem* inst = (CameraSystem*)lpParam;
	inst->UpdateCamera();
	return 0;
}

void CameraSystem::EnableFreeCam(bool bFreeCamMode) {
	if (bFreeCamMode) {
		//Prevents the gun from moving when you move your mouse by not calling the function that moves your reticle. 
		SadMemEdit::DisableInstruction(DisruptHook::pCallMoveReticleSubProc, 5);
		SadMemEdit::DisableInstruction(DisruptHook::pCameraFOVWrite, 5); //Prevents the fov from being overwritten.
		/*
		* This is byte code for reading the enum as 0 always.
		* This ensures mouse input does not get blocked by cutscenes, etc.
		*/
		SadMemEdit::WriteBytes({ 0xB8, 0x0, 0x0, 0x0, 0x0 }, DisruptHook::pReadActionMapEnum);
		/*
		* Disable the entire function/subproc by returning when it tries to read arguments.
		* 0xC3 (ret) is byte code for return.
		* We disable those functions entirely because we make our own View Matrix, Camera Position Control, etc...
		*/
		SadMemEdit::WriteBytes({ 0xC3 }, DisruptHook::pCameraPositionSubProc);
		SadMemEdit::WriteBytes({ 0xC3 }, DisruptHook::pViewMatrixSubProc);
		SadMemEdit::WriteBytes({ 0xC3 }, DisruptHook::pLensRenderpassSubProc1);
		SadMemEdit::WriteBytes({ 0xC3 }, DisruptHook::pLensRenderpassSubProc2);
		SadMemEdit::WriteBytes({ 0xC3 }, DisruptHook::pLensRenderpassSubProc3);
		//Initial Values for calculating the view matrix.
		fMovementSpeedFactor = 1;
		fRoll = XM_PIDIV2;
		fYaw = 0;
		fPitch = 0;
		//Initial Values for DOF and FOV.
		bLensDOFEnabled = 1;
		bNearDOFEnabled = 0;
		bFarDOFEnabled = 1;
		fLensFocusRange = 7;
		fLensFocalLength = 100;
		fFStop = 40;
		fCameraFOV = 1.4;
		memcpy(&vector3CameraPosition, LPVOID(DisruptHook::pCameraPosition), 12); //Load current camera position as the starting point.
		CameraSystem::bFreeCamMode = true;
	}
	else {
		//We just restore the original byte code for everything we have overwritten.
		SadMemEdit::WriteBytes({ 0x8B, 0x47, 0x30, 0xFF, 0xC8 }, DisruptHook::pReadActionMapEnum);
		SadMemEdit::WriteBytes({ 0xE8, 0x11, 0x27, 0xA6, 0xFE }, DisruptHook::pCallMoveReticleSubProc);
		SadMemEdit::WriteBytes({ 0xF3, 0x0F, 0x11, 0x49, 0x24 }, DisruptHook::pCameraFOVWrite);
		SadMemEdit::WriteBytes({ 0x8B }, DisruptHook::pCameraPositionSubProc);
		SadMemEdit::WriteBytes({ 0x48 }, DisruptHook::pViewMatrixSubProc);
		SadMemEdit::WriteBytes({ 0x48 }, DisruptHook::pLensRenderpassSubProc1);
		SadMemEdit::WriteBytes({ 0x48 }, DisruptHook::pLensRenderpassSubProc2);
		SadMemEdit::WriteBytes({ 0x48 }, DisruptHook::pLensRenderpassSubProc3);
		CameraSystem::bFreeCamMode = false;
	}
}

void CameraSystem::UpdateCamera() {
	while (true) {
		//If we die, try to reload autosave, etc.. Automatically disable the freecam.
		if (DisruptHook::bGameLoading) {
			EnableFreeCam(false);
			SadHook::bFreeCamMode = false;
		}

		/*
		* Update pointers and allow the player to write to them (using hotkeys)
		* if and only if we are in gameplay to avoid bad pointer reads (which lead to crashing).
		* While checking if a pointer is null is a valid approach, some pointers in Watch Dogs are bad without being null
		* so trying to read or write with them will lead to crashes.
		*/
		if (DisruptHook::bCameraLoaded && !DisruptHook::bGameLoading) {
			//Update Pointers
			DisruptHook::pCameraStruct = ((*(uint64_t*)((*(uint64_t*)((*(uint64_t*)(SadHook::GetImagebase() + 0x36B7CC0)) + 0x0)) + 0x10)) + 0x0);
			DisruptHook::pCameraFOV = DisruptHook::pCameraStruct + 0x2C;
			DisruptHook::pCameraPosition = DisruptHook::pCameraStruct + 0x0C;
			DisruptHook::pViewMatrix = DisruptHook::pCameraStruct + 0x4C;
			DisruptHook::pLensStruct = ((*(uint64_t*)((*(uint64_t*)((*(uint64_t*)(SadHook::GetImagebase() + 0x36B90D8)) + 0x0)) + 0x18)) + 0x0);
			DisruptHook::pLensDOFEnabled = DisruptHook::pLensStruct + 0x10;
			DisruptHook::pLensConditionals = DisruptHook::pLensStruct + 0x20;
			DisruptHook::pLensFloats = DisruptHook::pLensStruct + 0x28;

			if (bFreeCamMode) {
				//Construct a Rotation Matrix for each dimension.
				matrixX = XMMatrixRotationX(fRoll);
				matrixY = XMMatrixRotationY(fPitch);
				matrixZ = XMMatrixRotationZ(fYaw);
				/*
				* Multiply the three matrices together to produce a 3x3 view matrix
				* which is identifical to the one Watch_Dogs uses.
				*/
				matrixProduct = matrixX * matrixY * matrixZ;
				XMStoreFloat3x3(&matrixFinalViewMatrix, matrixProduct);
				//Construct float vectors for our DOF values.
				vector2LensConditionals = { bNearDOFEnabled, bFarDOFEnabled };
				vector3LensFloats = { fLensFocusRange, fLensFocalLength, fFStop };
				//Update Pitch and Yaw based on user mouse movement.
				//Code commented as we are using only keyboard for everything
				//fPitch -= DisruptHook::fMouseDisplacementY * 0.008;
				//fYaw += DisruptHook::fMouseDisplacementX * 0.008;
				//The remainder of the code is mostly "what to do" when user presses a certain key.
				if (GetAsyncKeyState(VK_NUMPAD8) && !GetAsyncKeyState(VK_SUBTRACT)) {
					//Move camera forward.
					vector3CameraPosition.x += 0.01 * matrixFinalViewMatrix._11 * fMovementSpeedFactor;
					vector3CameraPosition.y += 0.01 * matrixFinalViewMatrix._12 * fMovementSpeedFactor;
					vector3CameraPosition.z += 0.01 * matrixFinalViewMatrix._13 * fMovementSpeedFactor;
				}

				if (GetAsyncKeyState(VK_SUBTRACT)) {
					//Pitch camera upward
					if (GetAsyncKeyState(VK_NUMPAD8)) fPitch -= 0.001;
				}

				if (GetAsyncKeyState(VK_NUMPAD2) && !GetAsyncKeyState(VK_SUBTRACT)) {
					//Backwards.
					vector3CameraPosition.x -= 0.01 * matrixFinalViewMatrix._11 * fMovementSpeedFactor;
					vector3CameraPosition.y -= 0.01 * matrixFinalViewMatrix._12 * fMovementSpeedFactor;
					vector3CameraPosition.z -= 0.01 * matrixFinalViewMatrix._13 * fMovementSpeedFactor;
				}

				if (GetAsyncKeyState(VK_SUBTRACT)) {
					//Pitch camera downward
					if (GetAsyncKeyState(VK_NUMPAD2)) fPitch += 0.001;
				}

				if (GetAsyncKeyState(VK_NUMPAD4) && !GetAsyncKeyState(VK_SUBTRACT)) {
					//Left.
					vector3CameraPosition.x -= 0.01 * matrixFinalViewMatrix._12 * fMovementSpeedFactor;
					vector3CameraPosition.y += 0.01 * matrixFinalViewMatrix._11 * fMovementSpeedFactor;
				}

				if (GetAsyncKeyState(VK_SUBTRACT)) {
					//turn camera left
					if (GetAsyncKeyState(VK_NUMPAD4)) fYaw += 0.001;
				}

				if (GetAsyncKeyState(VK_NUMPAD6) && !GetAsyncKeyState(VK_SUBTRACT)) {
					//Right.
					vector3CameraPosition.x += 0.01 * matrixFinalViewMatrix._12 * fMovementSpeedFactor;
					vector3CameraPosition.y -= 0.01 * matrixFinalViewMatrix._11 * fMovementSpeedFactor;
				}

				if (GetAsyncKeyState(VK_SUBTRACT)) {
					//turn camera right
					if (GetAsyncKeyState(VK_NUMPAD6)) fYaw -= 0.001;
				}

				if (GetAsyncKeyState(VK_DIVIDE)) {
					//Increase freecam speed.
					fMovementSpeedFactor += 0.7;
					while (GetAsyncKeyState(VK_DIVIDE));
				}

				if (GetAsyncKeyState(VK_MULTIPLY)) {
					//Decrease freecam speed.
					fMovementSpeedFactor -= 0.7;
					while (GetAsyncKeyState(VK_MULTIPLY));
				}

				if (GetAsyncKeyState(VK_NUMPAD9) && !GetAsyncKeyState(VK_SUBTRACT)) {
					//Move camera upwards.
					vector3CameraPosition.z += 0.002;
				}

				if (GetAsyncKeyState(VK_NUMPAD3) && !GetAsyncKeyState(VK_SUBTRACT)) {
					//Move camera downwards.
					vector3CameraPosition.z -= 0.002;
				}

				if (GetAsyncKeyState(VK_NUMPAD7)) {
					//Roll left.
					fRoll += 0.001;
				}

				if (GetAsyncKeyState(VK_NUMPAD1)) {
					//Roll right.
					fRoll -= 0.001;
				}

				if (GetAsyncKeyState(VK_SUBTRACT)) {
					//Increase FOV, but we don't want to do it past a certain threshold otherwise shit gets funky.
					if (GetAsyncKeyState(VK_NUMPAD9) && fCameraFOV < 2.5) fCameraFOV += 0.002;
				}

				if (GetAsyncKeyState(VK_SUBTRACT)) {
					//Decrease FOV, yada yada shit gets funky if too low.
					if (GetAsyncKeyState(VK_NUMPAD3) && fCameraFOV > 0.1) fCameraFOV -= 0.002;
				}

				if (GetAsyncKeyState(VK_UP)) {
					//Increase Focus Range.
					fLensFocusRange += 0.02;
				}

				if (GetAsyncKeyState(VK_DOWN)) {
					//We don't want to decrease the focus range below 0.5 otherwise the screen is just a blur.
					if (fLensFocusRange > 0.5) fLensFocusRange -= 0.02;
				}

				if (GetAsyncKeyState(VK_RIGHT) && !GetAsyncKeyState(VK_MENU)) {
					//Increase focal stop.
					fFStop += 0.02;
				}

				//if (GetAsyncKeyState(VK_LEFT) && !GetAsyncKeyState(VK_MENU)) {
				//	//We don't want to decrease the focal stop below 1.0 otherwise the screen is just a blur as well.
				//	if (fFStop > 1.0) fFStop -= 0.02;
				}
				if (GetAsyncKeyState(VK_MENU)) {
					//Increase focal length.
					if (GetAsyncKeyState(VK_RIGHT)) fLensFocalLength += 1;
				}

				//if (GetAsyncKeyState(VK_MENU)) {
				//	//We don't want to decrease the focal length below 24 there is no or not much DoF visible.
				//	if (GetAsyncKeyState(VK_LEFT) && fLensFocalLength > 24) fLensFocalLength -= 1;
				}


				/*
				* This toggles between Near and Far DOF modes, each of those require certain lens settings initially
				* which is why we redefine all of them.
				*/
				if (GetAsyncKeyState(VK_NUMPAD5)) {
					if (!bNearDOFEnabled) {
						bLensDOFEnabled = 1;
						bNearDOFEnabled = 1;
						bFarDOFEnabled = 1;
						fLensFocusRange = 7;
						fLensFocalLength = 100;
						fFStop = 7.5;
					}
					else {
						bLensDOFEnabled = 1;
						bNearDOFEnabled = 0;
						bFarDOFEnabled = 1;
						fLensFocusRange = 7;
						fLensFocalLength = 100;
						fFStop = 40;
					}
					while (GetAsyncKeyState(VK_NUMPAD5));
				}

				//Update the camera position, view matrix, depth of field, and FOV once user input has been processed.
				memcpy(LPVOID(DisruptHook::pCameraPosition), &vector3CameraPosition, 12);
				memcpy(LPVOID(DisruptHook::pViewMatrix), &matrixFinalViewMatrix, 36);
				memcpy(LPVOID(DisruptHook::pLensDOFEnabled), &bLensDOFEnabled, 1);
				memcpy(LPVOID(DisruptHook::pLensConditionals), &vector2LensConditionals, 8);
				memcpy(LPVOID(DisruptHook::pLensFloats), &vector3LensFloats, 12);
				memcpy(LPVOID(DisruptHook::pCameraFOV), &fCameraFOV, 4);
			}
		}
		Sleep(4);
	}
}
