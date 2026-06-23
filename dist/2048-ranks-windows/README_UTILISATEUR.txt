2048 Helper - Windows version
================================

How to start:

1. Unzip the whole folder.
2. Double-click:

   Lancer 2048 Helper.bat

3. A web page opens.
4. Keep the black terminal window open while you play.
5. To stop, close the black terminal window.

You do not need g++ to use this version.
The folder only needs to contain:

2048-ranks.exe

If Windows shows a security warning:

- Allow the app on the private/local network.
- The app runs only on your computer: http://127.0.0.1:8765

If the launcher says Python is missing:

- You do not have the full package with embedded Python.
- Ask for the full zip, or install Python 3 from https://www.python.org/downloads/windows/

If the launcher says 2048-ranks.exe is missing:

- The zip is incomplete.
- Ask for the complete Windows package.
- End users should not have to compile this file.

If the AI does not work:

- Make sure 2048-ranks.exe is in the same folder as this README.
- If Windows blocked the file, right-click it, open Properties, and choose Unblock if the option appears.
- If antivirus quarantined it, restore it or ask for a fresh zip.

Games are saved in:

data\sessions\

The default session is:

ma_partie
