:: ## FastAPI is a Python web framework designed for building fast and efficient backend APIs
::   
::  FastAPI is built on top of Starlette, a lightweight ASGI framework that handles the core HTTP operations, 
::  including routing, middleware, and WebSockets support. Starlette provides the low-level tools that FastAPI uses to manage HTTP requests, 
::  making it a stable and performant foundation for building web applications
::
:: ## Uvicorn is a lightning-fast ASGI server, optimized for handling asynchronous code. It's essential for running FastAPI applications
::    because it handles incoming HTTP requests and manages the lifecycle of these requests
::

start http://localhost:8080
uvicorn server:app --reload --port 8080
