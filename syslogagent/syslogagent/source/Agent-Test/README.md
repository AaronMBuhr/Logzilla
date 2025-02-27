# Agent-Test - Unit Tests for LogZilla Syslog Agent

This project contains comprehensive unit tests for the LogZilla Syslog Agent Windows service using the Google Test framework.

## Test Structure

The tests are organized by component for better maintainability:

1. **ConfigurationTests** - Tests for the `Configuration` class, verifying loading from registry, handling of settings, etc.
2. **ServiceTests** - Tests for the Windows service functionality.
3. **EventLogTests** - Tests for the event log monitoring and event handling.
4. **NetworkClientTests** - Tests for the HTTP and JSON network clients.
5. **MessageQueueTests** - Tests for the message queue and batching systems.
6. **FileWatcherTests** - Tests for file watching functionality.

## Running Tests

To run the tests:

1. Build the Agent-Test project.
2. Run the test executable, which will execute all tests and report results.

## Test Philosophy

The tests are designed with the following principles:

1. **Isolation** - Each test should operate independently.
2. **Mocking** - External dependencies should be mocked when possible.
3. **Coverage** - Tests should aim for comprehensive code coverage.
4. **Structure** - Tests are organized by component to maintain clarity.

## Known Limitations

Some tests are marked as `DISABLED_` because they:

1. Require modifications to the code to make it more testable.
2. Depend on external resources (such as network connections or event logs).
3. Would benefit from more elaborate mocking infrastructure.

## Adding New Tests

When adding new tests:

1. Place them in the appropriate component test file.
2. Follow the existing naming conventions.
3. Add the test file to the project in Visual Studio.
4. For component-specific tests, create a new test file if needed.

## Testing Approach and Best Practices

### For Windows Service Testing

Testing a Windows service presents unique challenges, since the service control manager interactions are difficult to mock. The approach taken is to:

1. Test individual components in isolation.
2. Use dependency injection when possible to replace real components with mocks.
3. Structure the code to allow for testing without service manager dependencies.

### For Network Testing

Most of the network tests are designed to validate the behavior without actual network connections. This is achieved by:

1. Mocking the network client interfaces.
2. Simulating successful and failed connections.
3. Focusing on the behavior of the code under test, not the actual network operations.

### For File System Testing

File system tests use temporary files and directories to avoid impacting the actual system. The tests:

1. Create temporary files for testing.
2. Clean up after themselves.
3. Isolate the file watching behavior for testing.
