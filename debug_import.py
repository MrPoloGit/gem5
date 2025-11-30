print("--- Script Starting ---")
try:
    print("Attempting to import m5.objects...")
    from m5.objects import BingoPrefetcher
    print("Import Success! The issue is elsewhere.")
except ImportError as e:
    print(f"Import Error: {e}")
except SyntaxError as e:
    print(f"Syntax Error in SimObject definitions: {e}")
except Exception as e:
    print(f"General Error: {e}")
