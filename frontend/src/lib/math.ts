import type { Quaternion } from '@/types'

/**
 * Convert quaternion to Euler angles (pitch, roll, yaw) in degrees.
 * Uses ZYX rotation order (yaw-pitch-roll).
 */
export function quaternionToEuler(q: Quaternion): { pitch: number; roll: number; yaw: number } {
  const { W: w, X: x, Y: y, Z: z } = q

  // Roll (x-axis rotation)
  const sinr_cosp = 2 * (w * x + y * z)
  const cosr_cosp = 1 - 2 * (x * x + y * y)
  const roll = Math.atan2(sinr_cosp, cosr_cosp)

  // Pitch (y-axis rotation)
  const sinp = 2 * (w * y - z * x)
  let pitch: number
  if (Math.abs(sinp) >= 1) {
    pitch = Math.sign(sinp) * (Math.PI / 2)
  } else {
    pitch = Math.asin(sinp)
  }

  // Yaw (z-axis rotation)
  const siny_cosp = 2 * (w * z + x * y)
  const cosy_cosp = 1 - 2 * (y * y + z * z)
  const yaw = Math.atan2(siny_cosp, cosy_cosp)

  const toDegrees = (rad: number) => (rad * 180) / Math.PI
  return {
    pitch: toDegrees(pitch),
    roll: toDegrees(roll),
    yaw: toDegrees(yaw),
  }
}

/**
 * Convert radians to degrees.
 */
export function radToDeg(rad: number): number {
  return (rad * 180) / Math.PI
}

/**
 * Convert degrees to radians.
 */
export function degToRad(deg: number): number {
  return (deg * Math.PI) / 180
}
