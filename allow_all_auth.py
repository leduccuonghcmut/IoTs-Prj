from hbmqtt.plugins.authentication import BaseAuthPlugin

class AllowAllAuthPlugin(BaseAuthPlugin):
    async def authenticate(self, *args, **kwargs):
        # Allows any username/password
        username = kwargs.get('username', None)
        password = kwargs.get('password', None)
        # username and password are bytes; decode them if needed
        if username is not None:
            username = username.decode('utf-8')
        if password is not None:
            password = password.decode('utf-8')
        print(f"Client tried to connect with username={username}, password={password}")

        return True