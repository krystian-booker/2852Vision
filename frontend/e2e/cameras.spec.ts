import { test, expect } from '@playwright/test';
import { createCamera, navigateTo, resetDatabase } from './utils';

test.describe('Cameras Page', () => {
  test.beforeEach(async ({ page, request }) => {
    await resetDatabase(request);
    await navigateTo(page, '/cameras', 'page-title-cameras');
  });

  test('should load cameras page', async ({ page }) => {
    // Check page title
    await expect(page.getByTestId('page-title-cameras')).toBeVisible();

    // Check for Add Camera button
    await expect(page.getByRole('button', { name: 'Add Camera' })).toBeVisible();
  });

  test('should show empty state when no cameras configured', async ({ page }) => {
    // Check for empty state message
    await expect(page.locator('text=No cameras configured')).toBeVisible();
    await expect(page.locator('text=Click "Add Camera" to get started')).toBeVisible();
  });

  test('should open add camera modal', async ({ page }) => {
    // Click Add Camera button (the main one that opens the modal)
    await page.getByRole('button', { name: 'Add Camera' }).first().click();

    // Modal should appear
    await expect(page.getByTestId('add-camera-modal')).toBeVisible();
    await expect(page.getByRole('heading', { name: 'Add Camera' })).toBeVisible();
    await expect(page.locator('text=Configure a new camera device')).toBeVisible();

    // Check form fields exist
    await expect(page.locator('#camera-name')).toBeVisible();
    await expect(page.locator('#camera-type')).toBeVisible();
  });

  test('should close add camera modal on cancel', async ({ page }) => {
    // Open modal
    await page.getByRole('button', { name: 'Add Camera' }).first().click();
    await expect(page.getByTestId('add-camera-modal')).toBeVisible();

    // Click Cancel
    await page.locator('button:has-text("Cancel")').click();

    // Modal should close
    await expect(page.getByTestId('add-camera-modal')).not.toBeVisible();
  });

  test('should discover mock USB cameras', async ({ page }) => {
    // Open add camera modal
    await page.locator('button:has-text("Add Camera")').first().click();

    // Select camera type
    await page.locator('#camera-type').click();
    await page.locator('[role="option"]:has-text("USB Camera")').click();

    // Click Discover button
    await page.locator('button:has-text("Discover")').click();

    // Wait for discovery to complete
    await page.waitForTimeout(1000);

    // Check if mock devices appeared (should show select dropdown)
    const selectTrigger = page.locator('button[role="combobox"]:has-text("Select device")');
    await expect(selectTrigger).toBeVisible();
  });

  test('should add a USB camera successfully', async ({ page }) => {
    const cameraName = 'Test USB Camera';

    // Open modal
    await page.locator('button:has-text("Add Camera")').first().click();

    // Fill in camera name
    await page.locator('#camera-name').fill(cameraName);

    // Select camera type
    await page.locator('#camera-type').click();
    await page.locator('[role="option"]:has-text("USB Camera")').click();

    // Discover devices
    await page.locator('button:has-text("Discover")').click();
    await page.waitForTimeout(1000);

    // Select first mock device
    await page.locator('button[role="combobox"]:has-text("Select device")').click();
    await page.locator('[role="option"]:has-text("Mock USB Camera 0")').first().click();

    // Add camera (click submit button inside modal)
    await page.locator('button:has-text("Add Camera")').last().click();

    // Wait for success toast and modal to close
    await page.waitForTimeout(1500);

    // Modal should close
    await expect(page.locator('text=Configure a new camera device')).not.toBeVisible();

    // Camera should appear in table
    const cameraRow = page.locator('[data-testid="camera-row"]').filter({ hasText: cameraName });
    await expect(cameraRow).toBeVisible();
    await expect(cameraRow.locator('td').filter({ hasText: 'USB' }).first()).toBeVisible();
  });

  test('should show camera in table after adding', async ({ page }) => {
    const cameraName = 'Front Camera';

    // Add a camera first
    await page.locator('button:has-text("Add Camera")').first().click();
    await page.locator('#camera-name').fill(cameraName);
    await page.locator('#camera-type').click();
    await page.locator('[role="option"]:has-text("Spinnaker Camera (FLIR)")').click();
    await page.locator('button:has-text("Discover")').click();
    await page.waitForTimeout(1000);
    await page.locator('button[role="combobox"]:has-text("Select device")').click();
    await page.locator('[role="option"]:has-text("Mock Spinnaker Camera")').first().click();
    await page.locator('button:has-text("Add Camera")').last().click();
    await page.waitForTimeout(1500);

    // Check table has correct columns
    await expect(page.locator('th:has-text("Name")')).toBeVisible();
    await expect(page.locator('th:has-text("Type")')).toBeVisible();
    await expect(page.locator('th:has-text("Identifier")')).toBeVisible();
    await expect(page.locator('th:has-text("Status")')).toBeVisible();
    await expect(page.locator('th:has-text("Actions")')).toBeVisible();

    // Check camera appears in table
    await expect(page.locator('[data-testid="camera-row"]').filter({ hasText: cameraName })).toBeVisible();
  });

  test('should edit camera name', async ({ page, request }) => {
    const originalName = 'Original Camera';
    const newName = 'Updated Camera Name';

    await createCamera(request, {
      name: originalName,
      camera_type: 'USB',
      identifier: 'edit-usb-camera',
    });
    await page.reload();

    // Click edit button
    const editButton = page
      .locator('[data-testid="camera-row"]', { hasText: originalName })
      .getByRole('button', { name: `Edit ${originalName}` });
    await editButton.click();

    // Edit modal should open
    await expect(page.locator('text=Edit Camera')).toBeVisible();
    await expect(page.locator('text=Change camera name')).toBeVisible();

    // Clear and enter new name
    await page.locator('#edit-camera-name').clear();
    await page.locator('#edit-camera-name').fill(newName);

    // Save changes
    await page.locator('button:has-text("Save Changes")').click();
    await page.waitForTimeout(1000);

    // Check updated name appears
    const updatedRow = page.locator('[data-testid="camera-row"]').filter({ hasText: newName });
    await expect(updatedRow).toBeVisible();
    await expect(page.locator('[data-testid="camera-row"]').filter({ hasText: originalName })).not.toBeVisible();
  });

  test('should show delete confirmation dialog', async ({ page, request }) => {
    const cameraName = 'Camera to Delete';

    await createCamera(request, {
      name: cameraName,
      camera_type: 'Spinnaker',
      identifier: 'delete-spinnaker-camera',
    });
    await page.reload();

    // Click delete button (second button in actions column)
    await page
      .locator('[data-testid="camera-row"]', { hasText: cameraName })
      .getByRole('button', { name: `Delete ${cameraName}` })
      .click();

    // Delete confirmation should appear
    await expect(page.getByRole('heading', { name: 'Delete Camera' })).toBeVisible();
    await expect(page.locator(`text=Are you sure you want to delete "${cameraName}"`)).toBeVisible();
    await expect(page.locator('text=cannot be undone')).toBeVisible();

    // Cancel deletion
    await page.locator('button:has-text("Cancel")').click();

    // Camera should still be visible
    await expect(page.locator('[data-testid="camera-row"]').filter({ hasText: cameraName })).toBeVisible();
  });

  test('should delete camera successfully', async ({ page, request }) => {
    const cameraName = 'Camera to Remove';

    await createCamera(request, {
      name: cameraName,
      camera_type: 'RealSense',
      identifier: 'delete-realsense-camera',
    });
    await page.reload();

    // Confirm camera was added
    await expect(page.locator('[data-testid="camera-row"]').filter({ hasText: cameraName })).toBeVisible();

    // Click delete button
    await page
      .locator('[data-testid="camera-row"]', { hasText: cameraName })
      .getByRole('button', { name: `Delete ${cameraName}` })
      .click();

    // Confirm deletion
    await page.locator('button:has-text("Delete Camera")').click();
    await page.waitForTimeout(1000);

    // Camera should be removed
    await expect(page.locator('[data-testid="camera-row"]').filter({ hasText: cameraName })).not.toBeVisible();

    // Should show empty state
    await expect(page.locator('text=No cameras configured')).toBeVisible();
  });

  test('should show validation error when adding camera without name', async ({ page }) => {
    // Open modal
    await page.locator('button:has-text("Add Camera")').first().click();

    // Select type and device but leave name empty
    await page.locator('#camera-type').click();
    await page.locator('[role="option"]:has-text("USB Camera")').click();
    await page.locator('button:has-text("Discover")').click();
    await page.waitForTimeout(1000);
    await page.locator('button[role="combobox"]:has-text("Select device")').click();
    await page.locator('[role="option"]:has-text("Mock USB Camera 0")').first().click();

    // Try to add without name
    await page.locator('button:has-text("Add Camera")').last().click();
    await page.waitForTimeout(500);

    // Should show error toast (modal stays open)
    await expect(page.locator('text=Configure a new camera device')).toBeVisible();
  });

  test('should require device discovery before adding camera', async ({ page }) => {
    // Open modal
    await page.locator('button:has-text("Add Camera")').first().click();

    // Fill name and select type but don't discover
    await page.locator('#camera-name').fill('Test Camera');
    await page.locator('#camera-type').click();
    await page.locator('[role="option"]:has-text("USB Camera")').click();

    // Try to add without discovering/selecting device
    await page.locator('button:has-text("Add Camera")').last().click();
    await page.waitForTimeout(500);

    // Modal should stay open (validation error)
    await expect(page.locator('text=Configure a new camera device')).toBeVisible();
  });

  test('should handle all camera types', async ({ page }) => {
    const cameraTypes = [
      { type: 'USB Camera', mockDevice: 'Mock USB Camera 0' },
      { type: 'Spinnaker Camera (FLIR)', mockDevice: 'Mock Spinnaker Camera' },
      { type: 'Intel RealSense', mockDevice: 'Mock RealSense Camera' },
    ];

    for (const { type, mockDevice } of cameraTypes) {
      // Open modal
      await page.locator('button:has-text("Add Camera")').first().click();

      // Select camera type
      await page.locator('#camera-type').click();
      await page.locator(`[role="option"]:has-text("${type}")`).click();

      // Discover should work
      await page.locator('button:has-text("Discover")').click();
      await page.waitForTimeout(1000);

      // Mock device should be available
      await page.locator('button[role="combobox"]:has-text("Select device")').click();
      await expect(page.locator(`[role="option"]:has-text("${mockDevice}")`).first()).toBeVisible();

      // Close modal
      await page.keyboard.press('Escape');
      await page.locator('button:has-text("Cancel")').click();
      await page.waitForTimeout(200);
    }
  });

  test('should persist cameras across page reload', async ({ page }) => {
    const cameraName = 'Persistent Camera';

    // Add camera
    await page.locator('button:has-text("Add Camera")').first().click();
    await page.locator('#camera-name').fill(cameraName);
    await page.locator('#camera-type').click();
    await page.locator('[role="option"]:has-text("USB Camera")').click();
    await page.locator('button:has-text("Discover")').click();
    await page.waitForTimeout(1000);
    await page.locator('button[role="combobox"]:has-text("Select device")').click();
    await page.locator('[role="option"]:has-text("Mock USB Camera 0")').first().click();
    await page.locator('button:has-text("Add Camera")').last().click();
    await page.waitForTimeout(1500);

    // Reload page
    await page.reload();
    await page.waitForSelector('h1:has-text("Cameras")');
    await page.waitForTimeout(1000);

    // Camera should still be there
    await expect(page.locator('[data-testid="camera-row"]').filter({ hasText: cameraName })).toBeVisible();
  });
});
