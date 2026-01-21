---
name: Praktor HTTP Executor
description: Guide for using the HTTP executor to make API requests in Praktor workflows with JavaScript hooks
---

# Praktor HTTP Executor Skill

Use this skill to generate HTTP tasks in Praktor workflows with support for all HTTP methods, authentication, and JavaScript hooks for request modification and response validation.

## Quick Reference

### Basic HTTP Request
```yaml
tasks:
  - name: fetch_users
    http:
      url: "https://api.example.com/users"
      method: GET
      headers:
        Authorization: "Bearer {{ secrets.api_token }}"
```

### Supported HTTP Methods
All methods are case-insensitive:
- `GET` - Retrieve data
- `POST` - Create resources
- `PUT` - Update/replace resources
- `DELETE` - Remove resources
- `PATCH` - Partial updates
- `HEAD` - Retrieve headers only
- `OPTIONS` - Query supported methods

## HTTP Task Configuration

### Required Fields
- `url` (string): The HTTP endpoint URL
  - Supports variable substitution: `"https://api.example.com/users/{{ user_id }}"`

### Optional Fields
- `method` (string): HTTP method (default: `GET`)
- `headers` (map): Request headers
- `body` (string): Request body (for POST, PUT, PATCH)
- `timeout_ms` (integer): Request timeout in milliseconds
- `follow_redirects` (boolean): Follow HTTP redirects
- `auth_user` (string): Basic auth username
- `auth_pass` (string): Basic auth password
- `bearer_token` (string): Bearer token for Authorization header
- `script` (string): Pre-request JavaScript hook
- `test` (string): Post-response JavaScript hook

## Authentication Patterns

### Bearer Token
```yaml
- name: authenticated_request
  http:
    url: "https://api.example.com/protected"
    method: GET
    bearer_token: "{{ secrets.api_token }}"
```

### Basic Auth
```yaml
- name: basic_auth_request
  http:
    url: "https://api.example.com/protected"
    method: GET
    auth_user: "{{ secrets.username }}"
    auth_pass: "{{ secrets.password }}"
```

### Custom Headers
```yaml
- name: custom_auth
  http:
    url: "https://api.example.com/protected"
    method: GET
    headers:
      Authorization: "Custom {{ secrets.custom_token }}"
      X-API-Key: "{{ secrets.api_key }}"
```

## JavaScript Hooks

### Pre-Request Hook (`script`)
Modify the request before it's sent. Available object: `request`

**Request Object Properties:**
- `request.url` (read/write): Request URL
- `request.method` (read/write): HTTP method
- `request.body` (read/write): Request body
- `request.addHeader(key, value)`: Add a request header

**Example:**
```yaml
- name: dynamic_request
  http:
    url: "https://api.example.com/search"
    method: GET
    script: |
      // Modify URL with query parameters
      request.url = request.url + "?q=test&limit=10";
      
      // Add custom headers
      request.addHeader("X-Request-ID", Date.now().toString());
      request.addHeader("X-Client-Version", "1.0");
      
      // Change method if needed
      request.method = "POST";
      
      // Set body
      request.body = JSON.stringify({ query: "test" });
```

### Post-Response Hook (`test`)
Validate the response. Available object: `response`

**Response Object Properties:**
- `response.status` (read-only): HTTP status code
- `response.body` (read-only): Response body as string

**Available Functions:**
- `fail(message)`: Fail the task with an error message
- `console.log(...)`: Log messages
- `JSON.parse(str)`: Parse JSON strings
- `JSON.stringify(obj)`: Convert objects to JSON

**Example:**
```yaml
- name: validated_request
  http:
    url: "https://api.example.com/users"
    method: GET
    test: |
      // Validate status
      if (response.status !== 200) {
          fail("Expected 200 OK, got " + response.status);
      }
      
      // Parse and validate JSON
      var data = JSON.parse(response.body);
      if (!data.users || data.users.length === 0) {
          fail("No users returned");
      }
      
      // Validate specific fields
      if (data.users[0].email.indexOf("@") === -1) {
          fail("Invalid email format");
      }
```

### Success Override Behavior
When a `test` script is provided and passes:
- The task is marked as **successful** regardless of HTTP status code
- This allows testing error conditions (e.g., validating a 404 response)

```yaml
- name: test_404_handling
  http:
    url: "https://api.example.com/nonexistent"
    method: GET
    test: |
      // This task succeeds even though status is 404
      if (response.status !== 404) {
          fail("Expected 404 for nonexistent resource");
      }
```

## Output Variables

HTTP tasks automatically populate these output variables:

- `tasks.<task_name>.outputs.status` - HTTP status code
- `tasks.<task_name>.outputs.body` - Raw response body
- `tasks.<task_name>.outputs.data` - Parsed JSON response (if Content-Type is JSON)
- `tasks.<task_name>.outputs.success` - Boolean indicating success

**Example:**
```yaml
tasks:
  - name: get_user
    http:
      url: "https://api.example.com/users/123"
      method: GET
  
  - name: process_user
    depends_on: ["get_user"]
    script:
      source: |
        var user = context.get("tasks.get_user.outputs.data");
        print("User name:", user.name);
        print("User email:", user.email);
```

## Common Patterns

### Chained Requests
```yaml
tasks:
  # 1. Authenticate
  - name: login
    http:
      url: "https://api.example.com/auth/login"
      method: POST
      headers:
        Content-Type: "application/json"
      body: |
        {
          "username": "{{ env.API_USER }}",
          "password": "{{ env.API_PASS }}"
        }
      test: |
        var data = JSON.parse(response.body);
        if (!data.token) {
            fail("No token in response");
        }

  # 2. Use token from login
  - name: get_protected_data
    depends_on: [login]
    http:
      url: "https://api.example.com/protected/data"
      method: GET
      headers:
        Authorization: "Bearer {{ tasks.login.outputs.data.token }}"
```

### POST with JSON Body
```yaml
- name: create_user
  http:
    url: "https://api.example.com/users"
    method: POST
    headers:
      Content-Type: "application/json"
    body: |
      {
        "name": "{{ user_name }}",
        "email": "{{ user_email }}",
        "role": "admin"
      }
```

### Dynamic Request Building
```yaml
- name: dynamic_api_call
  http:
    url: "https://api.example.com/data"
    method: GET
    script: |
      // Build query string dynamically
      var filters = context.get("filters");
      var params = [];
      
      if (filters.status) {
          params.push("status=" + filters.status);
      }
      if (filters.limit) {
          params.push("limit=" + filters.limit);
      }
      
      if (params.length > 0) {
          request.url = request.url + "?" + params.join("&");
      }
```

### Error Handling
```yaml
- name: api_call_with_retry
  http:
    url: "https://api.example.com/flaky-endpoint"
    method: GET
    retries:
      count: 3
      delay: "5s"
    test: |
      if (response.status >= 500) {
          fail("Server error: " + response.status);
      }
      
      var data = JSON.parse(response.body);
      if (data.error) {
          fail("API error: " + data.error.message);
      }
```

## Limitations

### Binary Response Handling
⚠️ **Important:** Response bodies containing null bytes (`\0`) will be truncated at the first null byte.

**Workaround:** If your API returns binary data, ensure it's base64-encoded in the JSON response:
```json
{
  "filename": "image.png",
  "data": "iVBORw0KGgoAAAANSUhEUgAAAAUA..."
}
```

### Property Enumeration
Response object properties (`status`, `body`) are **non-enumerable** for technical reasons.

```javascript
// ❌ This won't work:
console.log(response);  // Shows: {}

// ✅ Use direct access instead:
console.log("Status:", response.status);
console.log("Body:", response.body);
```

## Best Practices

1. **Use `test` scripts for validation** - Don't rely solely on HTTP status codes
2. **Handle authentication tokens** - Store tokens in task outputs for reuse
3. **Set appropriate timeouts** - Prevent workflows from hanging
4. **Validate response structure** - Check for required fields in JSON responses
5. **Use variable substitution** - Keep workflows DRY and maintainable
6. **Add error context** - Use descriptive `fail()` messages

## Complete Example

```yaml
tasks:
  # 1. Authenticate
  - name: login
    http:
      url: "https://api.example.com/auth/login"
      method: POST
      headers:
        Content-Type: "application/json"
      body: |
        {
          "username": "{{ env.API_USER }}",
          "password": "{{ env.API_PASS }}"
        }
      test: |
        var data = JSON.parse(response.body);
        if (!data.token) {
            fail("No token in response");
        }

  # 2. Fetch data with authentication
  - name: get_users
    depends_on: [login]
    http:
      url: "https://api.example.com/users"
      method: GET
      headers:
        Authorization: "Bearer {{ tasks.login.outputs.data.token }}"
      test: |
        var users = JSON.parse(response.body);
        if (users.length === 0) {
            fail("No users found");
        }

  # 3. Create a new user
  - name: create_user
    depends_on: [login]
    http:
      url: "https://api.example.com/users"
      method: POST
      headers:
        Authorization: "Bearer {{ tasks.login.outputs.data.token }}"
        Content-Type: "application/json"
      script: |
        // Generate unique email
        var timestamp = Date.now();
        var userData = {
            name: "Test User",
            email: "test" + timestamp + "@example.com"
        };
        request.body = JSON.stringify(userData);
      test: |
        if (response.status !== 201) {
            fail("Expected 201 Created");
        }
        var user = JSON.parse(response.body);
        if (!user.id) {
            fail("No user ID in response");
        }
```
