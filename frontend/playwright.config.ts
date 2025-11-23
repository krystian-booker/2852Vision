import { defineConfig, devices } from '@playwright/test';

/**
 * See https://playwright.dev/docs/test-configuration.
 */
export default defineConfig({
  testDir: './e2e',
  fullyParallel: false, // Run tests serially to avoid conflicts
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 2 : 0,
  workers: 1, // Single worker to avoid conflicts with Flask backend
  reporter: 'html',
  use: {
    // Use explicit IPv4 to avoid ::1 resolution issues in some runners (e.g. VS Code extension)
    baseURL: 'http://127.0.0.1:8080',
    trace: 'on-first-retry',
    screenshot: 'only-on-failure',
  },

  projects: [
    {
      name: 'chromium',
      use: { ...devices['Desktop Chrome'] },
    },
  ],

  /* Run Flask backend before starting tests */
  webServer: {
    command: 'conda run -n 2852Vision --no-capture-output python run.py',
    url: 'http://localhost:8080',
    reuseExistingServer: !process.env.CI,
    timeout: 15000,  // Increased to 15 seconds for backend startup
    env: {
      FLASK_ENV: 'testing',
      CAMERA_THREADS_ENABLED: 'False',
      SKIP_VITE_START: 'true',  // Don't try to start Vite in testing mode
      E2E_TESTING: 'true',
    },
  },
});
