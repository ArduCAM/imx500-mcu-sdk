from contextlib import contextmanager

def PROFILE_FUNCTION(func):
    return func

@contextmanager
def PROFILE_SCOPE(_name: str):
    yield