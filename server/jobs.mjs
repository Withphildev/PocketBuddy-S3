import { randomUUID } from 'node:crypto';

const jobs = new Map();
const jobTtlMs = 15 * 60 * 1000;

function prune() {
  const cutoff = Date.now() - jobTtlMs;
  for (const [id, job] of jobs) {
    if (job.updatedAt < cutoff) jobs.delete(id);
  }
}

export function startJob(task) {
  prune();
  const id = randomUUID();
  const job = { id, state: 'queued', createdAt: Date.now(), updatedAt: Date.now() };
  jobs.set(id, job);

  Promise.resolve().then(async () => {
    job.state = 'working';
    job.updatedAt = Date.now();
    try {
      job.result = await task();
      job.state = 'complete';
    } catch (error) {
      job.error = error?.message || 'Buddy job failed';
      job.state = 'error';
    }
    job.updatedAt = Date.now();
  });
  return id;
}

export function jobStatus(id) {
  prune();
  const job = jobs.get(id);
  if (!job) return null;
  const status = { id: job.id, state: job.state };
  if (job.state === 'complete') status.result = job.result;
  if (job.state === 'error') status.error = job.error;
  return status;
}

export function resetJobsForTests() {
  jobs.clear();
}
