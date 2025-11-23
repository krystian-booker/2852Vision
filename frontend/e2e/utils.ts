import { expect, type APIRequestContext, type Page } from '@playwright/test'

export async function resetDatabase(request: APIRequestContext) {
  const response = await request.post('/test/reset-database')
  if (!response.ok()) {
    console.error('resetDatabase failed', response.status(), await response.text())
  }
  expect(response.ok()).toBeTruthy()
}

export async function seedTestCameras(request: APIRequestContext) {
  const response = await request.post('/test/seed-test-data')
  if (!response.ok()) {
    console.error('seedTestCameras failed', response.status(), await response.text())
  }
  expect(response.ok()).toBeTruthy()
}

export async function createCamera(
  request: APIRequestContext,
  camera: { name: string; camera_type: string; identifier: string; device_info_json?: string }
) {
  const response = await request.post('/cameras/add', {
    data: {
      name: camera.name,
      camera_type: camera.camera_type,
      identifier: camera.identifier,
      device_info_json: camera.device_info_json ?? '{}',
    },
  })
  expect(response.ok()).toBeTruthy()
}

export async function navigateTo(page: Page, path: string, headingTestId: string) {
  await page.goto(path)
  await expect(page.getByTestId(headingTestId)).toBeVisible()
}
