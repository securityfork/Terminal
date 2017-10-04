/********************************************************
*                                                       *
*   Copyright (C) Microsoft. All rights reserved.       *
*                                                       *
********************************************************/

#include "precomp.h"
#include <windows.h>
#include "terminalInput.hpp"

#include "strsafe.h"

#define WIL_SUPPORT_BITOPERATION_PASCAL_NAMES
#include <wil\Common.h>

#ifdef BUILD_ONECORE_INTERACTIVITY
#include "..\..\interactivity\inc\VtApiRedirection.hpp"
#endif

using namespace Microsoft::Console::VirtualTerminal;

DWORD const dwAltGrFlags = LEFT_CTRL_PRESSED | RIGHT_ALT_PRESSED;

TerminalInput::TerminalInput(_In_ std::function<void(std::deque<std::unique_ptr<IInputEvent>>&)> pfn)
{
    _pfnWriteEvents = pfn;
}

TerminalInput::~TerminalInput()
{

}

// See http://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h2-PC-Style-Function-Keys
//    For the source for these tables.
// Also refer to the values in terminfo for kcub1, kcud1, kcuf1, kcuu1, kend, khome.
//   the 'xterm' setting lists the application mode versions of these sequences.
const TerminalInput::_TermKeyMap TerminalInput::s_rgCursorKeysNormalMapping[]
{
    { VK_UP, L"\x1b[A" },
    { VK_DOWN, L"\x1b[B" },
    { VK_RIGHT, L"\x1b[C" },
    { VK_LEFT, L"\x1b[D" },
    { VK_HOME, L"\x1b[H" },
    { VK_END, L"\x1b[F" },
};

const TerminalInput::_TermKeyMap TerminalInput::s_rgCursorKeysApplicationMapping[]
{
    { VK_UP, L"\x1bOA" },
    { VK_DOWN, L"\x1bOB" },
    { VK_RIGHT, L"\x1bOC" },
    { VK_LEFT, L"\x1bOD" },
    { VK_HOME, L"\x1bOH" },
    { VK_END, L"\x1bOF" },
};

const TerminalInput::_TermKeyMap TerminalInput::s_rgKeypadNumericMapping[]
{
    // HEY YOU. UPDATE THE MAX LENGTH DEF WHEN YOU MAKE CHANGES HERE.
    { VK_BACK, L"\x7f"},
    { VK_PAUSE, L"\x1a" },
    { VK_ESCAPE, L"\x1b" },
    { VK_INSERT, L"\x1b[2~" },
    { VK_DELETE, L"\x1b[3~" },
    { VK_PRIOR, L"\x1b[5~" },
    { VK_NEXT, L"\x1b[6~" },
    { VK_F1, L"\x1bOP" }, // also \x1b[11~, PuTTY uses \x1b\x1b[A
    { VK_F2, L"\x1bOQ" }, // also \x1b[12~, PuTTY uses \x1b\x1b[B
    { VK_F3, L"\x1bOR" }, // also \x1b[13~, PuTTY uses \x1b\x1b[C
    { VK_F4, L"\x1bOS" }, // also \x1b[14~, PuTTY uses \x1b\x1b[D
    { VK_F5, L"\x1b[15~" },
    { VK_F6, L"\x1b[17~" },
    { VK_F7, L"\x1b[18~" },
    { VK_F8, L"\x1b[19~" },
    { VK_F9, L"\x1b[20~" },
    { VK_F10, L"\x1b[21~" },
    { VK_F11, L"\x1b[23~" },
    { VK_F12, L"\x1b[24~" },
};

//Application mode - Some terminals support both a "Numeric" input mode, and an "Application" mode
//  The standards vary on what each key translates to in the various modes, so I tried to make it as close
//  to the VT220 standard as possible.
//  The notable difference is in the arrow keys, which in application mode translate to "^[0A" (etc) as opposed to "^[[A" in numeric
//Some very unclear documentation at http://invisible-island.net/xterm/ctlseqs/ctlseqs.html also suggests alternate encodings for F1-4
//  which I have left in the comments on those entries as something to possibly add in the future, if need be.
//It seems to me as though this was used for early numpad implementations, where presently numlock would enable
//  "numeric" mode, outputting the numbers on the keys, while "application" mode does things like pgup/down, arrow keys, etc.
//These keys aren't translated at all in numeric mode, so I figured I'd leave them out of the numeric table.
const TerminalInput::_TermKeyMap TerminalInput::s_rgKeypadApplicationMapping[]
{
    // HEY YOU. UPDATE THE MAX LENGTH DEF WHEN YOU MAKE CHANGES HERE.
    { VK_BACK, L"\x7f" },
    { VK_PAUSE, L"\x1a" },
    { VK_ESCAPE, L"\x1b" },
    { VK_INSERT, L"\x1b[2~" },
    { VK_DELETE, L"\x1b[3~" },
    { VK_PRIOR, L"\x1b[5~" },
    { VK_NEXT, L"\x1b[6~" },
    { VK_F1, L"\x1bOP" }, // also \x1b[11~, PuTTY uses \x1b\x1b[A
    { VK_F2, L"\x1bOQ" }, // also \x1b[12~, PuTTY uses \x1b\x1b[B
    { VK_F3, L"\x1bOR" }, // also \x1b[13~, PuTTY uses \x1b\x1b[C
    { VK_F4, L"\x1bOS" }, // also \x1b[14~, PuTTY uses \x1b\x1b[D
    { VK_F5, L"\x1b[15~" },
    { VK_F6, L"\x1b[17~" },
    { VK_F7, L"\x1b[18~" },
    { VK_F8, L"\x1b[19~" },
    { VK_F9, L"\x1b[20~" },
    { VK_F10, L"\x1b[21~" },
    { VK_F11, L"\x1b[23~" },
    { VK_F12, L"\x1b[24~" },
    // The numpad has a variety of mappings, none of which seem standard or really configurable by the OS.
    // See http://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h2-PC-Style-Function-Keys
    //   to see just how convoluted this all is.
    // PuTTY uses a set of mappings that don't work in ViM without reamapping them back to the numpad
    // (see http://vim.wikia.com/wiki/PuTTY_numeric_keypad_mappings#Comments)
    // I think the best solution is to just not do any for the time being.
    // Putty also provides configuration for choosing which of the 5 mappings it has through the settings, which is more work than we can manage now.
    // { VK_MULTIPLY, L"\x1bOj" },     // PuTTY: \x1bOR (I believe putty is treating the top row of the numpad as PF1-PF4)
    // { VK_ADD, L"\x1bOk" },          // PuTTY: \x1bOl, \x1bOm (with shift)
    // { VK_SEPARATOR, L"\x1bOl" },    // ? I'm not sure which key this is...
    // { VK_SUBTRACT, L"\x1bOm" },     // \x1bOS
    // { VK_DECIMAL, L"\x1bOn" },      // \x1bOn
    // { VK_DIVIDE, L"\x1bOo" },       // \x1bOQ
    // { VK_NUMPAD0, L"\x1bOp" },
    // { VK_NUMPAD1, L"\x1bOq" },
    // { VK_NUMPAD2, L"\x1bOr" },
    // { VK_NUMPAD3, L"\x1bOs" },
    // { VK_NUMPAD4, L"\x1bOt" },
    // { VK_NUMPAD5, L"\x1bOu" }, // \x1b0E
    // { VK_NUMPAD5, L"\x1bOE" }, // PuTTY \x1b[G
    // { VK_NUMPAD6, L"\x1bOv" },
    // { VK_NUMPAD7, L"\x1bOw" },
    // { VK_NUMPAD8, L"\x1bOx" },
    // { VK_NUMPAD9, L"\x1bOy" },
    // { '=', L"\x1bOX" },      // I've also seen these codes mentioned in some documentation,
    // { VK_SPACE, L"\x1bO " }, //  but I wasn't really sure if they should be included or not...
    // { VK_TAB, L"\x1bOI" },   // So I left them here as a reference just in case.
};

// Sequences to send when a modifier is pressed with any of these keys
// Basically, the 'm' will be replaced with a character indicating which modifier keys are pressed.
const TerminalInput::_TermKeyMap TerminalInput::s_rgModifierKeyMapping[]
{
    // HEY YOU. UPDATE THE MAX LENGTH DEF WHEN YOU MAKE CHANGES HERE.
    { VK_UP, L"\x1b[1;mA" },
    { VK_DOWN, L"\x1b[1;mB" },
    { VK_RIGHT, L"\x1b[1;mC" },
    { VK_LEFT, L"\x1b[1;mD" },
    { VK_HOME, L"\x1b[1;mH" },
    { VK_END, L"\x1b[1;mF" },
    { VK_F1, L"\x1b[1;mP" },
    { VK_F2, L"\x1b[1;mQ" },
    { VK_F3, L"\x1b[1;mR" },
    { VK_F4, L"\x1b[1;mS" },
    { VK_F5, L"\x1b[15;m~" },
    { VK_F6, L"\x1b[17;m~" },
    { VK_F7, L"\x1b[18;m~" },
    { VK_F8, L"\x1b[19;m~" },
    { VK_F9, L"\x1b[20;m~" },
    { VK_F10, L"\x1b[21;m~" },
    { VK_F11, L"\x1b[23;m~" },
    { VK_F12, L"\x1b[24;m~" },
    // Ubuntu's inputrc also defines \x1b[5C, \x1b\x1bC (and D) as 'forward/backward-word' mappings
    // I believe '\x1b\x1bC' is listed because the C1 ESC (x9B) gets encoded as
    //  \xC2\x9B, but then translated to \x1b\x1b if the C1 codepoint isn't supported by the current encoding
};

// Do NOT include the null terminator in the count.
const size_t TerminalInput::_TermKeyMap::s_cchMaxSequenceLength = 7; // UPDATE THIS DEF WHEN THE LONGEST MAPPED STRING CHANGES

const size_t TerminalInput::s_cCursorKeysNormalMapping      = ARRAYSIZE(s_rgCursorKeysNormalMapping);
const size_t TerminalInput::s_cCursorKeysApplicationMapping = ARRAYSIZE(s_rgCursorKeysApplicationMapping);
const size_t TerminalInput::s_cKeypadNumericMapping         = ARRAYSIZE(s_rgKeypadNumericMapping);
const size_t TerminalInput::s_cKeypadApplicationMapping     = ARRAYSIZE(s_rgKeypadApplicationMapping);
const size_t TerminalInput::s_cModifierKeyMapping           = ARRAYSIZE(s_rgModifierKeyMapping);

void TerminalInput::ChangeKeypadMode(_In_ bool const fApplicationMode)
{
    _fKeypadApplicationMode = fApplicationMode;
}

void TerminalInput::ChangeCursorKeysMode(_In_ bool const fApplicationMode)
{
    _fCursorApplicationMode = fApplicationMode;
}

bool TerminalInput::s_IsShiftPressed(_In_ const KeyEvent& keyEvent)
{
    return IsFlagSet(keyEvent._activeModifierKeys, SHIFT_PRESSED);
}

bool TerminalInput::s_IsAltPressed(_In_ const KeyEvent& keyEvent)
{
    return IsAnyFlagSet(keyEvent._activeModifierKeys, ALT_PRESSED);
}

bool TerminalInput::s_IsCtrlPressed(_In_ const KeyEvent& keyEvent)
{
    return IsAnyFlagSet(keyEvent._activeModifierKeys, CTRL_PRESSED);
}

bool TerminalInput::s_IsModifierPressed(_In_ const KeyEvent& keyEvent)
{
    return IsAnyFlagSet(keyEvent._activeModifierKeys, MOD_PRESSED);
}

bool TerminalInput::s_IsCursorKey(_In_ const KeyEvent& keyEvent)
{
    // true iff vk in [End, Home, Left, Up, Right, Down]
    return (keyEvent._virtualKeyCode >= VK_END) && (keyEvent._virtualKeyCode <= VK_DOWN);
}

const size_t TerminalInput::GetKeyMappingLength(_In_ const KeyEvent& keyEvent) const
{
    size_t length = 0;
    if (s_IsCursorKey(keyEvent))
    {
        length = (_fCursorApplicationMode) ? s_cCursorKeysApplicationMapping : s_cCursorKeysNormalMapping;
    }
    else
    {
        length = (_fKeypadApplicationMode) ? s_cKeypadApplicationMapping : s_cKeypadNumericMapping;
    }
    return length;
}

const TerminalInput::_TermKeyMap* TerminalInput::GetKeyMapping(_In_ const KeyEvent& keyEvent) const
{
    const TerminalInput::_TermKeyMap* mapping = nullptr;

    if (s_IsCursorKey(keyEvent))
    {
        mapping = (_fCursorApplicationMode) ? s_rgCursorKeysApplicationMapping : s_rgCursorKeysNormalMapping;
    }
    else
    {
        mapping = (_fKeypadApplicationMode) ? s_rgKeypadApplicationMapping : s_rgKeypadNumericMapping;
    }
    return mapping;
}

// Routine Description:
// - Searches the s_ModifierKeyMapping for a entry corresponding to this key event.
//      Changes the second to last byte to correspond to the currently pressed modifier keys
//      before sending to the input.
// Arguments:
// - keyEvent - Key event to translate
// Return Value:
// - True if there was a match to a key translation, and we successfully modified and sent it to the input
bool TerminalInput::_SearchWithModifier(_In_ const KeyEvent& keyEvent) const
{

    const TerminalInput::_TermKeyMap* pMatchingMapping;
    bool fSuccess = _SearchKeyMapping(keyEvent,
                                      s_rgModifierKeyMapping,
                                      s_cModifierKeyMapping,
                                      &pMatchingMapping);
    if (fSuccess)
    {
        size_t cch = 0;
        if (SUCCEEDED(StringCchLengthW(pMatchingMapping->pwszSequence, _TermKeyMap::s_cchMaxSequenceLength + 1, &cch)) &&
            cch > 0)
        {
            wchar_t* rwchModifiedSequence = new wchar_t[cch + 1];
            if (rwchModifiedSequence != nullptr)
            {
                memcpy(rwchModifiedSequence, pMatchingMapping->pwszSequence, cch * sizeof(wchar_t));
                const bool fShift = s_IsShiftPressed(keyEvent);
                const bool fAlt = s_IsAltPressed(keyEvent);
                const bool fCtrl = s_IsCtrlPressed(keyEvent);
                rwchModifiedSequence[cch - 2] = L'1' + (fShift ? 1 : 0) + (fAlt ? 2 : 0) + (fCtrl ? 4 : 0);
                rwchModifiedSequence[cch] = 0;
                _SendInputSequence(rwchModifiedSequence);
                fSuccess = true;
                delete [] rwchModifiedSequence;
            }
        }
    }
    return fSuccess;
}

// Routine Description:
// - Searches the keyMapping for a entry corresponding to this key event, and returns it.
// Arguments:
// - keyEvent - Key event to translate
// - keyMapping - Array of key mappings to search
// - cKeyMapping - number of entries in keyMapping
// - pMatchingMapping - Where to put the pointer to the found match
// Return Value:
// - True if there was a match to a key translation
bool TerminalInput::_SearchKeyMapping(_In_ const KeyEvent& keyEvent,
                                      _In_reads_(cKeyMapping) const TerminalInput::_TermKeyMap* keyMapping,
                                      _In_ size_t const cKeyMapping,
                                      _Out_ const TerminalInput::_TermKeyMap** pMatchingMapping) const
{
    bool fKeyTranslated = false;
    for (size_t i = 0; i < cKeyMapping; i++)
    {
        const _TermKeyMap* const pMap = &(keyMapping[i]);

        if (pMap->wVirtualKey == keyEvent._virtualKeyCode)
        {
            fKeyTranslated = true;
            *pMatchingMapping = pMap;
            break;
        }
    }
    return fKeyTranslated;
}

// Routine Description:
// - Searches the input array of mappings, and sends it to the input if a match was found.
// Arguments:
// - keyEvent - Key event to translate
// - keyMapping - Array of key mappings to search
// - cKeyMapping - number of entries in keyMapping
// Return Value:
// - True if there was a match to a key translation, and we successfully sent it to the input
bool TerminalInput::_TranslateDefaultMapping(_In_ const KeyEvent& keyEvent,
                                             _In_reads_(cKeyMapping) const TerminalInput::_TermKeyMap* keyMapping,
                                             _In_ size_t const cKeyMapping) const
{
    const TerminalInput::_TermKeyMap* pMatchingMapping;
    bool fSuccess = _SearchKeyMapping(keyEvent, keyMapping, cKeyMapping, &pMatchingMapping);
    if (fSuccess)
    {
        _SendInputSequence(pMatchingMapping->pwszSequence);
        fSuccess = true;
    }
    return fSuccess;
}

bool TerminalInput::HandleKey(_In_ const IInputEvent* const pInEvent) const
{
    // By default, we fail to handle the key
    bool fKeyHandled = false;

    // On key presses, prepare to translate to VT compatible sequences
    if (pInEvent->EventType() == InputEventType::KeyEvent)
    {
        KeyEvent keyEvent = *static_cast<const KeyEvent* const>(pInEvent);

        // Only need to handle key down. See raw key handler (see RawReadWaitRoutine in stream.cpp)
        if (keyEvent._keyDown == TRUE)
        {
            // For AltGr enabled keyboards, the Windows system will
            // emit Left Ctrl + Right Alt as the modifier keys and
            // will have pretranslated the UnicodeChar to the proper
            // alternative value.
            // Through testing with Ubuntu, PuTTY, and Emacs for
            // Windows, it was discovered that any instance of Left
            // Ctrl + Right Alt will strip out those two modifiers and
            // send the unicode value straight through to the system.
            // Holding additional modifiers in addition to Left Ctrl +
            // Right Alt will then light those modifiers up again for
            // the unicode value.
            // Therefore to handle AltGr properly, our first step
            // needs to be to check if both Left Ctrl + Right Alt are
            // pressed...
            // ... and if they are both pressed, strip them out of the control key state.
            if (AreAllFlagsSet(keyEvent._activeModifierKeys, dwAltGrFlags))
            {
                ClearAllFlags(keyEvent._activeModifierKeys, dwAltGrFlags);
            }

            if (s_IsAltPressed(keyEvent) &&
                s_IsCtrlPressed(keyEvent) &&
                (keyEvent._charData == 0 || keyEvent._charData == 0x20) &&
                ((keyEvent._virtualKeyCode > 0x40 && keyEvent._virtualKeyCode <= 0x5A) ||
                 keyEvent._virtualKeyCode == VK_SPACE) )
            {
                // For Alt+Ctrl+Key messages, the UnicodeChar is NOT the Ctrl+key char, it's null.
                //      So we need to get the char from the vKey.
                //      EXCEPT for Alt+Ctrl+Space. Then the UnicodeChar is space, not NUL.
                wchar_t wchPressedChar = static_cast<wchar_t>(MapVirtualKeyW(keyEvent._virtualKeyCode, MAPVK_VK_TO_CHAR));
                // This is a trick - C-Spc is supposed to send NUL. So quick change space -> @ (0x40)
                wchPressedChar = (wchPressedChar == 0x20) ? 0x40 : wchPressedChar;
                if (wchPressedChar >= 0x40 && wchPressedChar < 0x7F)
                {
                    //shift the char to the ctrl range
                    wchPressedChar -= 0x40;
                    _SendEscapedInputSequence(wchPressedChar);
                    fKeyHandled = true;
                }
            }
            // ALT is a sequence of ESC + KEY.
            else if (keyEvent._charData != 0 && s_IsAltPressed(keyEvent))
            {
                _SendEscapedInputSequence(keyEvent._charData);
                fKeyHandled = true;
            }
            else if (s_IsCtrlPressed(keyEvent))
            {
                if ((keyEvent._charData == L' ' ) || // Ctrl+Space
                     // when Ctrl+@ comes through, the unicodechar
                     // will be '\x0', and the vkey will be
                     // VkKeyScanW(0), the vkey for null
                     (keyEvent._charData == L'\x0' && keyEvent._virtualKeyCode == LOBYTE(VkKeyScanW(0))))
                {
                    _SendNullInputSequence(keyEvent._activeModifierKeys);
                    fKeyHandled = true;
                }
            }

            // If a modifier key was pressed, then we need to try and send the modified sequence.
            if (!fKeyHandled && s_IsModifierPressed(keyEvent))
            {
                // Translate the key using the modifier table
                fKeyHandled = _SearchWithModifier(keyEvent);
            }

            if (!fKeyHandled)
            {
                // For perf optimization, filter out any typically printable Virtual Keys (e.g. A-Z)
                // This is in lieu of an O(1) sparse table or other such less-maintanable methods.
                // VK_CANCEL is an exception and we want to send the associated uChar as is.
                if ((keyEvent._virtualKeyCode < '0' || keyEvent._virtualKeyCode > 'Z') &&
                    keyEvent._virtualKeyCode != VK_CANCEL)
                {
                    fKeyHandled = _TranslateDefaultMapping(keyEvent, GetKeyMapping(keyEvent), GetKeyMappingLength(keyEvent));
                }
                else
                {
                    WCHAR rgwchSequence[2];
                    rgwchSequence[0] = keyEvent._charData;
                    rgwchSequence[1] = L'\0';
                    _SendInputSequence(rgwchSequence);
                    fKeyHandled = true;
                }
            }
        }
    }

    return fKeyHandled;
}

// Routine Description:
// - Sends the given char as a sequence representing Alt+wch, also the same as
//      Meta+wch.
// Arguments:
// - wch - character to send to input paired with Esc
// Return Value:
// - None
void TerminalInput::_SendEscapedInputSequence(_In_ const wchar_t wch) const
{
    try
    {
        std::deque<std::unique_ptr<IInputEvent>> inputEvents;
        inputEvents.push_back(std::make_unique<KeyEvent>(TRUE, 1ui16, 0ui16, 0ui16, L'\x1b', 0));
        inputEvents.push_back(std::make_unique<KeyEvent>(TRUE, 1ui16, 0ui16, 0ui16, wch, 0));
        _pfnWriteEvents(inputEvents);
    }
    catch (...)
    {
        LOG_HR(wil::ResultFromCaughtException());
    }
}

void TerminalInput::_SendNullInputSequence(_In_ DWORD const dwControlKeyState) const
{
    try
    {
        std::deque<std::unique_ptr<IInputEvent>> inputEvents;
        inputEvents.push_back(std::make_unique<KeyEvent>(TRUE,
                                                         1ui16,
                                                         LOBYTE(VkKeyScanW(0)),
                                                         0ui16,
                                                         L'\x0',
                                                         dwControlKeyState));
        _pfnWriteEvents(inputEvents);
    }
    catch (...)
    {
        LOG_HR(wil::ResultFromCaughtException());
    }
}

void TerminalInput::_SendInputSequence(_In_ PCWSTR const pwszSequence) const
{
    size_t cch = 0;
    // + 1 to max sequence length for null terminator count which is required by StringCchLengthW
    if (SUCCEEDED(StringCchLengthW(pwszSequence, _TermKeyMap::s_cchMaxSequenceLength + 1, &cch)) && cch > 0)
    {
        try
        {
            std::deque<std::unique_ptr<IInputEvent>> inputEvents;
            for (size_t i = 0; i < cch; i++)
            {
                inputEvents.push_back(std::make_unique<KeyEvent>(TRUE, 1ui16, 0ui16, 0ui16, pwszSequence[i], 0));
            }
            _pfnWriteEvents(inputEvents);
        }
        catch (...)
        {
            LOG_HR(wil::ResultFromCaughtException());
        }
    }
}
