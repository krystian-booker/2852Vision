import { test, expect } from '@playwright/test';
import { navigateTo, resetDatabase } from './utils';

test.describe('Settings Page', () => {
  test.beforeEach(async ({ page, request }) => {
    await resetDatabase(request);
    await navigateTo(page, '/settings', 'page-title-settings');
    // Wait for settings to load from API
    await page.waitForTimeout(1000);
  });

  test('should load settings page', async ({ page }) => {
    // Check page title
    await expect(page.getByTestId('page-title-settings')).toBeVisible();

    // Check main tabs exist
    await expect(page.getByRole('tab', { name: 'Global' })).toBeVisible();
    await expect(page.getByRole('tab', { name: 'Spinnaker' })).toBeVisible();
    await expect(page.getByRole('tab', { name: 'AprilTag Fields' })).toBeVisible();
    await expect(page.getByRole('tab', { name: 'System' })).toBeVisible();
  });

  test('should save and persist team number', async ({ page }) => {
    const testTeamNumber = '1234';

    // Find team number input by id
    const teamNumberInput = page.locator('#team-number');

    // Clear and enter new value
    await teamNumberInput.clear();
    await teamNumberInput.fill(testTeamNumber);

    // Save settings
    await page.locator('button:has-text("Save Global Settings")').click();

    // Wait for success toast
    await page.waitForTimeout(1000);

    // Reload the page
    await page.reload();
    await expect(page.getByTestId('page-title-settings')).toBeVisible();

    // Wait for settings to load
    await page.waitForTimeout(1000);

    // Verify the value persisted
    const savedValue = await page.locator('#team-number').inputValue();
    expect(savedValue).toBe(testTeamNumber);
  });

  test('should save and persist hostname', async ({ page }) => {
    const testHostname = 'test-2852Vision-device';

    // Find hostname input by id
    const hostnameInput = page.locator('#hostname');

    // Clear and enter new value
    await hostnameInput.clear();
    await hostnameInput.fill(testHostname);

    // Save settings
    await page.locator('button:has-text("Save Global Settings")').click();

    // Wait for save
    await page.waitForTimeout(1000);

    // Reload the page
    await page.reload();
    await expect(page.getByTestId('page-title-settings')).toBeVisible();

    // Wait for settings to load
    await page.waitForTimeout(1000);

    // Verify the value persisted
    const savedValue = await page.locator('#hostname').inputValue();
    expect(savedValue).toBe(testHostname);
  });

  test('should toggle IP mode between DHCP and Static', async ({ page }) => {
    // Set to DHCP
    await page.locator('#ip-mode').click();
    await page.locator('[role="option"]:has-text("DHCP")').click();
    await page.locator('button:has-text("Save Global Settings")').click();
    await page.waitForTimeout(1000);

    // Reload and verify DHCP
    await page.reload();
    await expect(page.getByTestId('page-title-settings')).toBeVisible();
    await page.waitForTimeout(1000);

    // Check the select shows DHCP
    await expect(page.locator('#ip-mode')).toContainText('DHCP');

    // Set to Static
    await page.locator('#ip-mode').click();
    await page.locator('[role="option"]:has-text("Static IP")').click();
    await page.locator('button:has-text("Save Global Settings")').click();
    await page.waitForTimeout(1000);

    // Reload and verify Static
    await page.reload();
    await expect(page.getByTestId('page-title-settings')).toBeVisible();
    await page.waitForTimeout(1000);
    await expect(page.locator('#ip-mode')).toContainText('Static IP');

    // Reset to DHCP
    await page.locator('#ip-mode').click();
    await page.locator('[role="option"]:has-text("DHCP")').click();
    await page.locator('button:has-text("Save Global Settings")').click();
  });

  test('should save all global settings together', async ({ page }) => {
    const testData = {
      team_number: '5678',
      hostname: '2852Vision-test',
    };

    // Fill all fields
    await page.locator('#team-number').clear();
    await page.locator('#team-number').fill(testData.team_number);
    await page.locator('#hostname').clear();
    await page.locator('#hostname').fill(testData.hostname);
    await page.locator('#ip-mode').click();
    await page.locator('[role="option"]:has-text("DHCP")').click();

    // Save
    await page.locator('button:has-text("Save Global Settings")').click();
    await page.waitForTimeout(1000);

    // Reload
    await page.reload();
    await expect(page.getByTestId('page-title-settings')).toBeVisible();
    await page.waitForTimeout(1000);

    // Verify all values persisted
    expect(await page.locator('#team-number').inputValue()).toBe(testData.team_number);
    expect(await page.locator('#hostname').inputValue()).toBe(testData.hostname);
    await expect(page.locator('#ip-mode')).toContainText('DHCP');
  });

  test('should clear values when saved as empty', async ({ page }) => {
    // First set some values
    await page.locator('#team-number').clear();
    await page.locator('#team-number').fill('9999');
    await page.locator('button:has-text("Save Global Settings")').click();
    await page.waitForTimeout(1000);

    // Now clear them
    await page.locator('#team-number').clear();
    await page.locator('button:has-text("Save Global Settings")').click();
    await page.waitForTimeout(1000);

    // Reload
    await page.reload();
    await expect(page.getByTestId('page-title-settings')).toBeVisible();
    await page.waitForTimeout(1000);

    // Verify empty
    const value = await page.locator('#team-number').inputValue();
    expect(value).toBe('');
  });

  test('should have Spinnaker SDK tab', async ({ page }) => {
    // Click Spinnaker tab
    await page.locator('button[role="tab"]:has-text("Spinnaker")').click();

    // Check Spinnaker content is visible
    await expect(page.locator('text=Spinnaker SDK')).toBeVisible();

    // SDK status should be displayed
    await expect(page.locator('text=SDK Status')).toBeVisible();
  });

  test('should show Spinnaker SDK status', async ({ page }) => {
    // Navigate to Spinnaker tab
    await page.locator('button[role="tab"]:has-text("Spinnaker")').click();

    // Should show SDK availability status
    const statusText = page.locator('text=Spinnaker SDK is available').or(page.locator('text=Spinnaker SDK not available'));
    await expect(statusText).toBeVisible();

    // Refresh status button should exist
    await expect(page.locator('button:has-text("Refresh Status")')).toBeVisible();
  });

  test('should show supported cameras list', async ({ page }) => {
    // Navigate to Spinnaker tab
    await page.locator('button[role="tab"]:has-text("Spinnaker")').click();

    // Should show supported cameras
    await expect(page.locator('text=Supported Cameras')).toBeVisible();
    await expect(page.locator('text=Teledyne FLIR Chameleon3')).toBeVisible();
    await expect(page.locator('text=Teledyne FLIR Blackfly')).toBeVisible();
  });

  test('should save and persist AprilTag field selection', async ({ page }) => {
    // Navigate to AprilTag Fields tab
    await page.locator('button[role="tab"]:has-text("AprilTag Fields")').click();

    // Select a field layout
    await page.locator('#field-select').click();
    await page.locator('[role="option"]:has-text("2024 Crescendo")').click();
    await page.waitForTimeout(500);

    // Reload page
    await page.reload();
    await expect(page.getByTestId('page-title-settings')).toBeVisible();
    await page.waitForTimeout(1000);

    // Navigate back to AprilTag Fields tab
    await page.locator('button[role="tab"]:has-text("AprilTag Fields")').click();

    // Verify selection persisted
    await expect(page.locator('#field-select')).toContainText('2024 Crescendo');

    // Test switching to another field
    await page.locator('#field-select').click();
    await page.locator('[role="option"]:has-text("2023 Charged Up")').click();
    await page.waitForTimeout(500);

    // Reload and verify
    await page.reload();
    await expect(page.getByTestId('page-title-settings')).toBeVisible();
    await page.waitForTimeout(1000);
    await page.locator('button[role="tab"]:has-text("AprilTag Fields")').click();
    await expect(page.locator('#field-select')).toContainText('2023 Charged Up');
  });

  test('should have system control buttons', async ({ page }) => {
    // Click System tab
    await page.locator('button[role="tab"]:has-text("System")').click();

    // Check for control buttons
    await expect(page.locator('button:has-text("Restart Application")')).toBeVisible();
    await expect(page.locator('button:has-text("Reboot Device")')).toBeVisible();
    await expect(page.locator('button:has-text("Factory Reset")')).toBeVisible();
  });

  test('should show confirmation dialog for restart and allow cancel', async ({ page }) => {
    // Click System tab
    await page.locator('button[role="tab"]:has-text("System")').click();

    // Click restart button
    await page.locator('button:has-text("Restart Application")').click();

    // Confirmation dialog should appear
    await expect(page.locator('text=Confirm Action')).toBeVisible();
    await expect(page.locator('text=temporarily interrupt all camera feeds')).toBeVisible();

    // Click cancel
    await page.locator('button:has-text("Cancel")').click();

    // Dialog should close
    await expect(page.locator('text=Confirm Action')).not.toBeVisible();
  });

  test('should show confirmation dialog for reboot and allow cancel', async ({ page }) => {
    // Click System tab
    await page.locator('button[role="tab"]:has-text("System")').click();

    // Click reboot button
    await page.locator('button:has-text("Reboot Device")').click();

    // Confirmation dialog should appear
    await expect(page.locator('text=Confirm Action')).toBeVisible();
    await expect(page.locator('text=All processes will be stopped')).toBeVisible();

    // Click cancel
    await page.locator('button:has-text("Cancel")').click();

    // Dialog should close
    await expect(page.locator('text=Confirm Action')).not.toBeVisible();
  });

  test('should show confirmation dialog for factory reset and allow cancel', async ({ page }) => {
    // Click System tab
    await page.locator('button[role="tab"]:has-text("System")').click();

    // Click factory reset button
    await page.locator('button:has-text("Factory Reset")').click();

    // Confirmation dialog should appear with warning
    await expect(page.locator('text=Confirm Action')).toBeVisible();
    await expect(page.locator('text=cannot be undone')).toBeVisible();

    // Click cancel
    await page.locator('button:has-text("Cancel")').click();

    // Dialog should close
    await expect(page.locator('text=Confirm Action')).not.toBeVisible();
  });

  test('should have export and import database buttons', async ({ page }) => {
    // Click System tab
    await page.locator('button[role="tab"]:has-text("System")').click();

    // Check for database buttons
    await expect(page.locator('button:has-text("Export Database")')).toBeVisible();
    await expect(page.locator('button:has-text("Import Database")')).toBeVisible();
  });

  test('should navigate between settings tabs', async ({ page }) => {
    // Navigate through tabs
    await page.locator('button[role="tab"]:has-text("Global")').click();
    await expect(page.getByRole('heading', { name: 'Global Settings' })).toBeVisible();

    await page.locator('button[role="tab"]:has-text("Spinnaker")').click();
    await expect(page.getByRole('heading', { name: 'Spinnaker SDK' })).toBeVisible();

    await page.locator('button[role="tab"]:has-text("AprilTag Fields")').click();
    await expect(page.getByRole('heading', { name: 'AprilTag Field Layouts' })).toBeVisible();

    await page.locator('button[role="tab"]:has-text("System")').click();
    await expect(page.getByRole('heading', { name: 'System Controls' })).toBeVisible();
  });
});
